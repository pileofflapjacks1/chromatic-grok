import { defineConfig, loadEnv } from "vite";
import { tanstackStart } from "@tanstack/react-start/plugin/vite";
import viteReact from "@vitejs/plugin-react";
import tailwindcss from "@tailwindcss/vite";

export default defineConfig(({ mode }) => {
  const fileEnv = loadEnv(mode, process.cwd(), "");
  for (const key of ["XAI_API_KEY", "XAI_MODEL"]) {
    if (fileEnv[key] && !process.env[key]) process.env[key] = fileEnv[key];
  }
  return {
    server: {
      port: 3000,
      host: "127.0.0.1",
    },
    resolve: {
      tsconfigPaths: true,
    },
    plugins: [tanstackStart(), viteReact(), tailwindcss()],
  };
});
