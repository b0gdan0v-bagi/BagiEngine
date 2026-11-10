# The static server of the web build (tools/web/CLAUDE.md). The build context is the deploy folder that build_and_deploy.py stages:
# this file, serve.py and the dist/ folder made by package.py. Railway sets PORT; serve.py listens on it.
FROM python:3.12-slim

WORKDIR /app
COPY serve.py ./
COPY dist/ dist/

ENV PYTHONUNBUFFERED=1
EXPOSE 8080
USER nobody
# No switch for the cross-origin isolation: serve.py sends Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy by itself for a
# threaded page (manifest.json of dist/ has "threads": true, or no manifest), which the threaded web build needs for its workers.
# A single-thread dist/ (package.py --single-thread) is served without them. test_serve.py starts this very command and checks both.
CMD ["python", "serve.py", "--root", "dist"]
