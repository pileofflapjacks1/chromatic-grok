import { createFileRoute } from "@tanstack/react-router";
import { Companion } from "../components/companion.tsx";

export const Route = createFileRoute("/")({
  component: Companion,
});
