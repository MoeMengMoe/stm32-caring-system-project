import logging

from .config import load_config
from .mqtt_service import HeartbeatService


def main() -> None:
    config = load_config()
    logging.basicConfig(
        level=getattr(logging, config.log_level, logging.INFO),
        format="%(asctime)s %(levelname)s %(name)s: %(message)s",
    )
    HeartbeatService(config).run_forever()


if __name__ == "__main__":
    main()
