import { getQuestion } from "../../../lib/api";
import PracticeClient from "./PracticeClient";

type Props = {
  params: { id: string };
};

export default async function PracticePage({ params }: Props) {
  const questionID = Number(params.id);
  const data = await getQuestion(questionID);

  return <PracticeClient question={data.question} status={data.status} />;
}
