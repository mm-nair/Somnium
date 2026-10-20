FROM python:3.12-slim-bookworm

ENV PYTHONDONTWRITEBYTECODE=1 \
    PYTHONUNBUFFERED=1 \
    PIP_DISABLE_PIP_VERSION_CHECK=1 \
    SOMNIUM_HOST=0.0.0.0 \
    SOMNIUM_PORT=8080 \
    SOMNIUM_OUTPUT_DIR=/data/outputs \
    PYTHONPATH=/app/server

WORKDIR /app

COPY server/requirements.txt /app/server/requirements.txt
RUN pip install --no-cache-dir -r /app/server/requirements.txt \
    && useradd --create-home --uid 10001 somnium

COPY server /app/server
COPY creative/creative.json /app/creative/creative.json
RUN chmod 755 /app/server/docker-entrypoint.sh \
    && mkdir -p /data/outputs \
    && chown -R somnium:somnium /app /data

EXPOSE 8080

HEALTHCHECK --interval=30s --timeout=5s --start-period=20s --retries=3 \
    CMD python -c "import os,urllib.request; urllib.request.urlopen('http://127.0.0.1:%s/health' % os.environ.get('SOMNIUM_PORT','8080'), timeout=3)"

ENTRYPOINT ["/app/server/docker-entrypoint.sh"]
