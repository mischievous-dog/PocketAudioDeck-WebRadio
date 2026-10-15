from pathlib import Path
import sys

Import("env")

project_dir = Path(env.subst("$PROJECT_DIR"))
local_deps = project_dir / ".pio-python-deps"

if local_deps.exists():
    if str(local_deps) not in sys.path:
        sys.path.insert(0, str(local_deps))
    env["ENV"]["PYTHONPATH"] = str(local_deps)
    env["ENV"]["PYTHONNOUSERSITE"] = "1"
    print(f"[pio_env] PYTHONPATH={local_deps}")
else:
    print("[pio_env] using PlatformIO Python environment")

# Auto-fix for esptool 5.0.0 get_metavar compatibility with click 8.2+
for esptool_cli in [
    project_dir / ".pio-core" / "packages" / "tool-esptoolpy" / "esptool" / "cli_util.py",
    Path.home() / ".platformio" / "packages" / "tool-esptoolpy" / "esptool" / "cli_util.py",
]:
    if esptool_cli.exists():
        try:
            content = esptool_cli.read_text(encoding="utf-8")
            if 'self.metavar = f"[{self.type.get_metavar(None) or self.type.name.upper()}]"' in content:
                patched = content.replace(
                    'self.metavar = f"[{self.type.get_metavar(None) or self.type.name.upper()}]"',
                    'metavar = None\n            if hasattr(self.type, "get_metavar"):\n                try:\n                    metavar = self.type.get_metavar(self, None)\n                except TypeError:\n                    try:\n                        metavar = self.type.get_metavar(self)\n                    except TypeError:\n                        metavar = None\n            self.metavar = f"[{metavar or self.type.name.upper()}]"',
                )
                esptool_cli.write_text(patched, encoding="utf-8")
                print(f"[pio_env] patched {esptool_cli} for click get_metavar compatibility")
        except Exception as e:
            print(f"[pio_env] warning: could not patch esptool cli_util: {e}")
