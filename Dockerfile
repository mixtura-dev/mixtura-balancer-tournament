FROM python:3.13-slim

WORKDIR /app

COPY pyproject.toml ./

RUN pip install --upgrade pip
RUN pip install uv
RUN uv pip install --system --editable .

COPY . .

ENV PYTHONUNBUFFERED=1

CMD ["faststream", "run", "mixtura_balancer_tournament:app"]