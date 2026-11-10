// Railway settings of the todo-api service (Infrastructure as Code; railway.json is not read after 2026-12-01).
// The partial "todo" owns only todo-api: apply leaves Vikunja, Postgres and the web services of the project alone.
// Variables stay unmanaged: they are set with `railway variables` (tools/todo/CLAUDE.md, "Railway"), never here.
//   cd tools/todo && npm install && railway config plan && railway config apply
import { defineRailway, project, service } from "railway/iac";

export const partial = "todo";

export default defineRailway(() => {
  const todoApi = service("todo-api", {
    build: { builder: "DOCKERFILE", dockerfilePath: "Dockerfile" },
    deploy: { restartPolicyType: "ON_FAILURE" },
    healthcheck: "/healthz",
    healthcheckTimeout: 60,
    // One replica: the mirror's worker runs inside the web process
    replicas: { sfo: 1 },
  });

  return project("vikunja-bagi-engine", {
    variables: { managed: false },
    resources: [todoApi],
  });
});
