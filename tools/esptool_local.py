"""Use the project's P4 v3.2-compatible esptool without altering ESP-IDF."""
from pathlib import Path
import runpy
import sys

package_dir = Path(__file__).resolve().parent / "esptool-4.12.0"
if not (package_dir / "esptool" / "__init__.py").is_file():
    raise SystemExit("Install esptool==4.12.0 into tools/esptool-4.12.0 first.")
sys.path.insert(0, str(package_dir))
runpy.run_module("esptool", run_name="__main__")
