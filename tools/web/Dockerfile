# The static server of the web build (tools/web/CLAUDE.md). The build context is the deploy folder that build_and_deploy.py stages:
# this file, serve.py and the dist/ folder made by package.py. Railway sets PORT; serve.py listens on it.
FROM python:3.12-slim

WORKDIR /app
COPY serve.py ./
COPY dist/ dist/

ENV PYTHONUNBUFFERED=1
EXPOSE 8080
USER nobody
CMD ["python", "serve.py", "--root", "dist"]
