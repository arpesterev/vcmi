#!/usr/bin/env python3
"""Check published V13 binaries, then run the native compatibility battles."""

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import urllib.request
from xml.etree import ElementTree as ET


RELEASE = "https://github.com/smanolloff/vcmi-gym/releases/download/v1.2/"
MODELS = {
    "attacker": {
        "name": "attacker-nkjrmrsq-202509291846-stochastic.onnx",
        "sha256": "f6804fbaaa93192a04130678a37a7093e68b11086687af6a364b56d6e0905177",
        "side": "0",
    },
    "defender": {
        "name": "defender-tukbajrv-202509241418-stochastic.onnx",
        "sha256": "ed40d72677f892bc31b89e325445b760be9393210b47a8db6d403a53cb40e107",
        "side": "1",
    },
}
MODEL_BYTES = 22402684
EXPECTED_TESTS = {
    "mmaiV13AttackerCompletesBattle",
    "mmaiV13DefenderCompletesBattle",
    "mmaiV13BothNeuralSidesCompleteBattle",
    "mmaiV13SiegeDefenderCompletesBattle",
    "mmaiV13StrictNeuralDoesNotCastHeroSpell",
}
INPUTS = [("obs", 1, [None]), ("ei_flat", 7, [2, None]),
          ("ea_flat", 1, [None, 1]), ("lengths", 6, [7])]
OUTPUTS = [("act0_probs", 1, [4]), ("hex1_probs", 1, [4, 165]),
           ("hex2_probs", 1, [165, 165]), ("act0_mask", 6, [4]),
           ("hex1_mask", 6, [4, 165]), ("hex2_mask", 6, [165, 165])]


def require_tensors(actual, expected):
    if len(actual) != len(expected):
        raise ValueError(f"Tensor count mismatch: {actual}")
    for tensor, (name, dtype, shape) in zip(actual, expected):
        if tensor["name"] != name or tensor["type"] != dtype:
            raise ValueError(f"Unexpected V13 tensor: {tensor}; expected {name}, type {dtype}")
        dims = tensor["shape"]
        if len(dims) != len(shape):
            raise ValueError(f"Unexpected V13 rank: {tensor}")
        for actual_dim, expected_dim in zip(dims, shape):
            if isinstance(actual_dim, int) and (actual_dim < 0 or
                    (expected_dim is not None and actual_dim != expected_dim)):
                raise ValueError(f"Unexpected V13 shape: {tensor}; expected {shape}")


def require_metadata(metadata, role):
    if metadata.get("version") != "13" or metadata.get("side") != MODELS[role]["side"]:
        raise ValueError(f"V13 version/side mismatch for {role}")
    table = json.loads(metadata["action_table"])
    if not isinstance(table, list) or len(table) != 4:
        raise ValueError("V13 action_table must contain four primary actions")
    for plane in table:
        if not isinstance(plane, list) or len(plane) != 165:
            raise ValueError("V13 action_table must contain 165 hex1 rows")
        for row in plane:
            if not isinstance(row, list) or len(row) != 165:
                raise ValueError("V13 action_table must contain 165 hex2 entries")
            if any(type(value) is not int or value < -(2 ** 31) or value >= 2312 for value in row):
                raise ValueError("V13 action_table contains an invalid action ID")
    return {"version": metadata["version"], "side": metadata["side"],
            "action_table_shape": [4, 165, 165]}


def verified_model(role, directory):
    spec = MODELS[role]
    destination = directory / spec["name"]
    url = RELEASE + spec["name"]
    if not destination.exists():
        with urllib.request.urlopen(url, timeout=120) as response:
            with destination.open("wb") as output:
                shutil.copyfileobj(response, output)
    data = destination.read_bytes()
    digest = hashlib.sha256(data).hexdigest()
    if len(data) != MODEL_BYTES or digest != spec["sha256"]:
        raise ValueError(f"Published {role} model size/SHA-256 mismatch: {len(data)}, {digest}")
    import onnx

    model = onnx.load_model_from_string(data)

    def tensors(values):
        return [{"name": value.name, "type": value.type.tensor_type.elem_type,
                 "shape": [dim.dim_value if dim.HasField("dim_value") else dim.dim_param or None
                           for dim in value.type.tensor_type.shape.dim]} for value in values]

    record = {"role": role, "url": url, "sha256": digest, "bytes": len(data),
              "ir_version": model.ir_version,
              "opsets": [{"domain": item.domain, "version": item.version}
                         for item in model.opset_import],
              "inputs": tensors(model.graph.input), "outputs": tensors(model.graph.output)}
    # Release prose and tagged exporter disagree about mask dtype and opset.
    # Record the hashed binary's declarations before validating the runtime ABI.
    print("MMAI_V13_BINARY " + json.dumps(record, sort_keys=True), flush=True)
    metadata = {item.key: item.value for item in model.metadata_props}
    if len(metadata) != len(model.metadata_props):
        raise ValueError("Duplicate ONNX metadata keys")
    record.update(require_metadata(metadata, role))
    require_tensors(record["inputs"], INPUTS)
    require_tensors(record["outputs"], OUTPUTS)
    return record


def require_battle_report(path, expected=EXPECTED_TESTS):
    cases = list(ET.parse(path).getroot().iter("testcase"))
    if len(cases) != len(expected) or {case.get("name") for case in cases} != expected:
        raise ValueError("V13 compatibility battle cases missing or duplicated")
    if any(case.find(tag) is not None for case in cases for tag in ("failure", "error", "skipped")):
        raise ValueError("A V13 compatibility battle failed or was skipped")


def run_battles(binary, directory):
    data_home = Path(os.environ.get("XDG_DATA_HOME", Path.home() / ".local/share"))
    config_home = Path(os.environ.get("XDG_CONFIG_HOME", Path.home() / ".config"))
    fixture = data_home / "vcmi/Mods/mmai-v13-test"
    if fixture.exists():
        raise ValueError("V13 CI fixture already exists; use a clean CI runner")
    (fixture / "models").mkdir(parents=True)
    (fixture / "config").mkdir()
    manifest = {
        "name": "MMAI V13 compatibility test fixture", "description": "CI-only legacy ONNX fixture",
        "version": "1.0", "modType": "AI", "author": "VCMI test suite",
        "filesystem": {"MMAI/CONFIG/": [{"path": "/config", "type": "dir"}],
                       "MMAI/MODELS/": [{"path": "/models", "type": "dir"}]},
    }
    (fixture / "mod.json").write_text(json.dumps(manifest) + "\n")
    settings = {"models": {}, "temperature": 0.0, "seed": 1337, "fallback": "BattleAI"}
    for role, spec in MODELS.items():
        shutil.copyfile(directory / spec["name"], fixture / "models" / spec["name"])
        settings["models"][role] = spec["name"]
    (fixture / "config/mmai-settings.json").write_text(json.dumps(settings) + "\n")

    # The old published mod retains its 1.7 compatibility declaration.
    # Only this separate fixture is mounted, in a fresh vcmitest process.
    preset_file = config_home / "vcmi/testModSettings.json"
    original = preset_file.read_bytes() if preset_file.exists() else None
    preset = json.loads(original) if original is not None else {}
    active = preset.setdefault("activePreset", "default")
    entry = preset.setdefault("presets", {}).setdefault(active, {})
    entry["mods"] = [mod for mod in entry.get("mods", ["vcmi", "vcmi-test"])
                     if mod != "mmai" and not mod.startswith("mmai.")] + ["mmai-v13-test"]
    preset_file.parent.mkdir(parents=True, exist_ok=True)
    report = directory / "mmai-v13-battles.xml"
    try:
        preset_file.write_text(json.dumps(preset) + "\n")
        settings_file = fixture / "config/mmai-settings.json"
        (fixture / "models/corrupt.onnx").write_bytes(b"invalid ONNX regression fixture\n")
        invalid = {
            "absent": {},
            "wrong-side": {"attacker": MODELS["defender"]["name"]},
            "load-error": {"attacker": "corrupt.onnx"},
        }
        for scenario, models in invalid.items():
            settings_file.write_text(json.dumps({**settings, "models": models}) + "\n")
            negative_report = directory / f"mmai-v13-{scenario}.xml"
            subprocess.run([str(binary),
                            "--gtest_filter=AIIntegrationNativeBattle.mmaiV13StrictRejectsInvalidPack",
                            "--gtest_output=xml:" + str(negative_report)], cwd=binary.parent,
                           env={**os.environ, "VCMI_AI_MMAI_V13_INVALID_PACK": scenario},
                           timeout=90, check=True)
            require_battle_report(negative_report, {"mmaiV13StrictRejectsInvalidPack"})
        settings_file.write_text(json.dumps(settings) + "\n")
        selected = ":".join("AIIntegrationNativeBattle." + name for name in sorted(EXPECTED_TESTS))
        subprocess.run([str(binary), "--gtest_filter=" + selected,
                        "--gtest_output=xml:" + str(report)], cwd=binary.parent,
                       env={**os.environ, "VCMI_AI_MMAI_V13_TEST": "1"},
                       timeout=300, check=True)
        require_battle_report(report)
    finally:
        if original is None:
            preset_file.unlink(missing_ok=True)
        else:
            preset_file.write_bytes(original)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--work-dir", type=Path, required=True)
    parser.add_argument("--test-binary", type=Path, required=True)
    args = parser.parse_args()
    directory = args.work_dir.resolve()
    directory.mkdir(parents=True, exist_ok=True)
    records = [verified_model(role, directory) for role in MODELS]
    (directory / "mmai-v13-abi.json").write_text(json.dumps(records, indent=2) + "\n")
    print("Verified published V13 binary ABI; native battle execution begins", flush=True)
    run_battles(args.test_binary.resolve(), directory)
    print("Verified five native V13 battles and three strict pack rejections; no learned hero-spell claim",
          flush=True)


if __name__ == "__main__":
    main()
