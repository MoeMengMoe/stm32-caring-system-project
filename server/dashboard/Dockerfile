ARG BASE_IMAGE=server-analysis_service:latest
FROM ${BASE_IMAGE}

WORKDIR /app

COPY src ./dashboard_src

ENV PYTHONUNBUFFERED=1

CMD ["python", "-m", "dashboard_src.main"]
