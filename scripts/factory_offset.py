"""Work around pioarduino 55.03.312 parsing only ota_0 application offsets."""
import csv
from pathlib import Path
Import("env")


def configure_factory(source=None, target=None, env=env):
    table = Path(env.subst("$PROJECT_DIR")) / "partitions.csv"
    with table.open() as stream:
        rows = ([field.strip() for field in row] for row in csv.reader(stream))
        partition = next(row for row in rows if len(row) >= 5 and row[0] == "factory" and row[1:3] == ["app", "factory"])
    offset, size = int(partition[3], 0), int(partition[4], 0)
    env.Replace(ESP32_APP_OFFSET=hex(offset))
    env.BoardConfig().update("upload.maximum_size", size)
    env["INTEGRATION_EXTRA_DATA"].update(application_offset=hex(offset))


configure_factory()
# The platform's size-check pre-action overwrites the address. Restore it before
# generating a combined image and before upload, including a cached/nobuild run.
env.AddPostAction("checkprogsize", configure_factory)
env.AddPreAction("upload", configure_factory)
