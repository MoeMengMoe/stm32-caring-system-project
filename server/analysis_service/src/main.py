import logging

from .config import load_config
from .mqtt_client import MqttStatusIngestor
from .repository import Repository


def main() -> None:
    config = load_config()
    logging.basicConfig(
        level=getattr(logging, config.log_level, logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )

    repository = Repository(config.db_path)
    repository.initialize()

    MqttStatusIngestor(config, repository).run_forever()


if __name__ == "__main__":
    main()
