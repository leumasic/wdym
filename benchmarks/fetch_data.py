import hashlib
import json
import sys
import time
import urllib.request
from pathlib import Path

ROWS_URL = "https://datasets-server.huggingface.co/rows?dataset=HuggingFaceFW/fineweb&config=sample-10BT&split=train"
ROWS_PER_REQUEST = 100


def fetch_rows(offset, length):
    url = f"{ROWS_URL}&offset={offset}&length={length}"
    request = urllib.request.Request(url, headers={"User-Agent": "wdym-benchmarks"})
    for attempt in range(8):
        try:
            with urllib.request.urlopen(request, timeout=60) as response:
                return json.load(response)["rows"]
        except Exception:
            time.sleep(2 + attempt * 3)
    sys.exit(f"could not fetch rows at offset {offset}")


def fetch_documents(offset, document_count, path):
    with open(path, "w", encoding="utf-8", newline="") as file:
        fetched_count = 0
        while fetched_count < document_count:
            length = min(ROWS_PER_REQUEST, document_count - fetched_count)
            for row in fetch_rows(offset + fetched_count, length):
                file.write(row["row"]["text"].rstrip("\n") + "\n")
            fetched_count += length

    sha256 = hashlib.sha256(path.read_bytes()).hexdigest()
    print(f"{path}: {document_count} documents, {path.stat().st_size} bytes, sha256 {sha256}")


data_directory = Path(sys.argv[1])
data_directory.mkdir(parents=True, exist_ok=True)
fetch_documents(0, 1_599, data_directory / "test.txt")
fetch_documents(1_000_000, 16_255, data_directory / "train.txt")
