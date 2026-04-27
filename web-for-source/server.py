from pathlib import Path
from urllib.parse import urlparse
import re

from flask import Flask, jsonify, request, send_from_directory
from flask_cors import CORS
from playwright.sync_api import TimeoutError as PlaywrightTimeoutError
from playwright.sync_api import sync_playwright


BASE_DIR = Path(__file__).resolve().parent
KEYWORDS = ("gemini", "chatgpt")

app = Flask(__name__, static_folder=str(BASE_DIR), static_url_path="")
CORS(app)


def normalize_url(raw_url: str) -> str:
    url = (raw_url or "").strip()
    if not url:
        raise ValueError("请提供网址")
    if not re.match(r"^https?://", url, re.I):
        url = f"https://{url}"
    return url


def parse_price(text: str):
    cleaned = (text or "").replace(",", "").replace("，", "")
    match = re.search(r"(?:¥|￥|\$)\s*(\d+(?:\.\d+)?)", cleaned)
    if match:
        return float(match.group(1))

    matches = re.findall(r"\d+(?:\.\d+)?", cleaned)
    if not matches:
        return None

    for value in matches:
        if "." in value or len(value) <= 5:
            return float(value)
    return float(matches[0])


def parse_stock(text: str):
    cleaned = (text or "").replace(",", "").replace("，", "")
    match = re.search(r"(?:库存|剩余|余量|数量|库存量|stock)[：:\s]*(\d+)", cleaned, re.I)
    if match:
        return int(match.group(1))
    return None


def extract_site_name(page, url: str) -> str:
    domain = urlparse(url).netloc
    title = (page.title() or "").strip()

    for selector in (
        'meta[property="og:site_name"]',
        'meta[name="application-name"]',
        'meta[name="apple-mobile-web-app-title"]',
    ):
        try:
            content = page.get_attribute(selector, "content")
            if content and content.strip():
                return content.strip()
        except Exception:
            pass

    for sep in ("-", "|", "_"):
        if sep in title:
            parts = [item.strip() for item in title.split(sep) if item.strip()]
            if parts:
                return parts[-1]

    return title or domain or url


def contains_target_keyword(name: str) -> bool:
    lowered = (name or "").strip().lower()
    return any(keyword in lowered for keyword in KEYWORDS)


def dedupe_products(products):
    unique = []
    seen = set()
    for product in products:
        name = (product.get("name") or "").strip()
        if not name:
            continue

        key = (
            name.lower(),
            product.get("price"),
            product.get("stock"),
        )
        if key in seen:
            continue
        seen.add(key)
        unique.append(
            {
                "name": name,
                "price": product.get("price"),
                "stock": product.get("stock"),
                "keyword": "gemini" if "gemini" in name.lower() else "chatgpt",
            }
        )
    return unique


def extract_products(page):
    selector = ",".join(
        [
            ".goods-item",
            ".product-item",
            ".shop-item",
            ".card-item",
            ".commodity-item",
            ".item-card",
            ".el-card",
            ".van-card",
            ".goods-card",
            ".sku",
            ".sku-item",
            ".card",
            "li",
            '[class*="goods"]',
            '[class*="product"]',
            '[class*="sku"]',
            '[class*="item"]',
        ]
    )

    items = page.eval_on_selector_all(
        selector,
        """
        nodes => nodes.map(node => {
          const text = (node.innerText || "").trim();
          const query = selectors => {
            for (const current of selectors) {
              const el = node.querySelector(current);
              if (el && el.innerText && el.innerText.trim()) {
                return el.innerText.trim();
              }
            }
            return "";
          };

          return {
            text,
            name: query([
              ".name", ".title", ".goods-name", ".product-name",
              ".sku-name", ".card-title", "h1", "h2", "h3", "h4",
              '[class*="name"]', '[class*="title"]'
            ]),
            priceText: query([
              ".price", ".goods-price", ".product-price",
              ".sku-price", ".amount", ".cost", '[class*="price"]'
            ]),
            stockText: query([
              ".stock", ".goods-stock", ".inventory",
              ".num", ".count", ".surplus", '[class*="stock"]', '[class*="count"]'
            ])
          };
        }).filter(item => item.text)
        """,
    )

    products = []
    for item in items:
        text = item.get("text", "")
        if not contains_target_keyword(text):
            continue

        lines = [line.strip() for line in text.splitlines() if line.strip()]
        name = (item.get("name") or "").strip()
        if not name:
            for line in lines:
                if contains_target_keyword(line):
                    name = line
                    break

        if not contains_target_keyword(name):
            continue

        price = parse_price(item.get("priceText") or text)
        stock = parse_stock(item.get("stockText") or text)
        if price is None and stock is None:
            continue

        products.append(
            {
                "name": name,
                "price": price,
                "stock": stock,
            }
        )

    if products:
        return dedupe_products(products)

    body_text = page.inner_text("body")
    body_lines = [line.strip() for line in body_text.splitlines() if line.strip()]
    fallback = []

    for index, line in enumerate(body_lines):
        if not contains_target_keyword(line):
            continue

        window = body_lines[index : index + 4]
        combined = " ".join(window)
        price = parse_price(combined)
        stock = parse_stock(combined)
        if price is None and stock is None:
            continue

        fallback.append(
            {
                "name": line,
                "price": price,
                "stock": stock,
            }
        )

    return dedupe_products(fallback)


def solve_challenge_if_needed(page):
    html = page.content()
    if "acw_sc__v2" not in html:
        return

    page.wait_for_timeout(2500)
    try:
        page.wait_for_load_state("networkidle", timeout=10000)
    except PlaywrightTimeoutError:
        pass
    page.wait_for_timeout(1500)


def scrape_page(url: str):
    with sync_playwright() as playwright:
        browser = playwright.chromium.launch(headless=True)
        context = browser.new_context(
            user_agent=(
                "Mozilla/5.0 (Windows NT 10.0; Win64; x64) "
                "AppleWebKit/537.36 (KHTML, like Gecko) "
                "Chrome/124.0.0.0 Safari/537.36"
            ),
            viewport={"width": 1440, "height": 1280},
            locale="zh-CN",
        )
        page = context.new_page()

        try:
            page.goto(url, wait_until="domcontentloaded", timeout=30000)
            solve_challenge_if_needed(page)
            page.wait_for_timeout(2000)

            site_name = extract_site_name(page, url)
            products = extract_products(page)

            return {
                "site_name": site_name,
                "url": url,
                "products": products[:30],
            }
        finally:
            context.close()
            browser.close()


@app.get("/")
def index():
    return send_from_directory(BASE_DIR, "index.html")


@app.post("/parse")
def parse():
    body = request.get_json(silent=True) or {}

    try:
        url = normalize_url(body.get("url", ""))
        result = scrape_page(url)
        return jsonify(result)
    except ValueError as exc:
        return jsonify({"error": str(exc)}), 400
    except Exception as exc:
        return jsonify({"error": f"解析失败: {exc}"}), 500


if __name__ == "__main__":
    print("服务启动: http://127.0.0.1:5000")
    app.run(host="0.0.0.0", port=5000, debug=False)
