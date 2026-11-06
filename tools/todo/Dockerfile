# The todo service. Build context: tools/todo (`railway up` from this folder).
FROM python:3.13-slim

ENV PYTHONDONTWRITEBYTECODE=1 PYTHONUNBUFFERED=1
WORKDIR /app
COPY service/requirements.txt service/requirements.txt
RUN pip install --no-cache-dir -r service/requirements.txt
COPY todo_markdown.py ./
COPY service ./service

# Railway sets PORT. One replica: the mirror's worker runs inside the web process.
CMD ["sh", "-c", "exec uvicorn --factory service.app:create_app_from_env --host 0.0.0.0 --port ${PORT:-8080} --proxy-headers --forwarded-allow-ips '*'"]
