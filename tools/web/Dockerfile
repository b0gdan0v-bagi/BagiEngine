# The static server of the web build (tools/web/CLAUDE.md). The build context is the deploy folder that build_and_deploy.py stages:
# this file, serve.py and the dist/ folder made by package.py. Railway sets PORT; serve.py listens on it.
FROM python:3.12-slim

WORKDIR /app
COPY serve.py ./
COPY dist/ dist/

ENV PYTHONUNBUFFERED=1
EXPOSE 8080
USER nobody
# The match log endpoint is off unless the service sets MATCH_STORE (tools/web/CLAUDE.md, "Match logs"). Its folder is on a Railway volume,
# which is mounted root-owned, so `nobody` cannot write there: the service needs RAILWAY_RUN_UID=0, or the server stops at start with
# "match store is not writable". Nothing in this image prepares the folder (the mount would hide it).
# No switch for the cross-origin isolation: serve.py sends Cross-Origin-Opener-Policy and Cross-Origin-Embedder-Policy by itself for a
# threaded page (manifest.json of dist/ has "threads": true, or no manifest), which the threaded web build needs for its workers.
# A single-thread dist/ (package.py --single-thread) is served without them. test_serve.py starts this very command and checks both.
CMD ["python", "serve.py", "--root", "dist"]
