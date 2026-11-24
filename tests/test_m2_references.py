#!/usr/bin/env python3
"""Immutable MATLAB M2 vector audit and offline CLI compatibility test."""
import hashlib
import json
import os
from pathlib import Path
import shlex
import subprocess
import tempfile

ROOT = Path(__file__).resolve().parents[1]
REF = ROOT / "references" / "m2_phase_axis"
CLI = shlex.split(os.environ.get("FFTTEST_COMMAND", str(ROOT / "build" / "ffttest-host")))
CASES = {
    "boundary_values_n2048", "minus_j_n2048", "negative_unity_n2048",
    "plus_j_n2048", "radarsat1_s3_n2048", "random_phase_n2048",
    "unity_phase_n2048",
}

actual = {p.name for p in REF.iterdir() if p.is_dir()}
assert actual == CASES, ("unexpected M2 reference cases", actual ^ CASES)

results = []
for name in sorted(CASES):
    case = REF / name
    manifest_path = case / "m2_manifest.json"
    manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    assert manifest["schema"] == "m2-phase-axis-test-v1"
    assert manifest["test_id"] == name
    assert manifest["sample_count"] == 2048
    assert manifest["maximum_supported_samples"] == 4096
    assert manifest["vector_len_coefficient"] == 2048
    assert manifest["vector_len_data"] == 2048
    assert manifest["validation"]["binary_readback_bit_exact"] is True
    assert manifest["validation"]["independent_integer_reference_bit_exact"] is True
    assert manifest["validation"]["file_lengths_ok"] is True
    listed = {entry["name"]: entry for entry in manifest["files"]}
    assert set(listed) == {"m2_input.bin", "m2_coeff.bin", "m2_expected.bin", "m2_reference.mat"}
    for filename, entry in listed.items():
        payload = case / filename
        data = payload.read_bytes()
        assert len(data) == entry["bytes"], payload
        assert hashlib.sha256(data).hexdigest() == entry["sha256"], payload
    for filename in ("m2_input.bin", "m2_coeff.bin", "m2_expected.bin"):
        assert listed[filename]["bytes"] == 8192
    with tempfile.TemporaryDirectory() as td:
        output = Path(td) / "must-not-be-created.bin"
        cmd = CLI + [
            "--validate-m2", "--samples", "2048",
            "--input", str(case / "m2_input.bin"),
            "--coeff", str(case / "m2_coeff.bin"),
            "--output", str(output),
            "--contract", str(ROOT / "m2_hardware_contract.json"),
            "--manifest", str(manifest_path),
        ]
        proc = subprocess.run(cmd, text=True, capture_output=True, timeout=15)
        assert proc.returncode == 0, (name, proc.stdout, proc.stderr)
        assert "offline validation PASS" in proc.stdout
        assert "schema=m2-phase-axis-test-v1" in proc.stdout
        assert not output.exists(), "offline validation created an output file"
    results.append(name)

# The candidate contract is sufficient for offline protocol/data validation,
# but must still reject real execution before any hardware access.
case = REF / sorted(CASES)[0]
with tempfile.TemporaryDirectory() as td:
    d = Path(td)
    base = json.loads((case / "m2_manifest.json").read_text())
    common = ["--samples", "2048", "--input", str(case / "m2_input.bin"),
              "--coeff", str(case / "m2_coeff.bin"), "--output", str(d / "must-not-exist.bin"),
              "--contract", str(ROOT / "m2_hardware_contract.json")]
    proc = subprocess.run(CLI + ["--run-m2"] + common + ["--manifest", str(case / "m2_manifest.json")],
                          text=True, capture_output=True, timeout=15)
    assert proc.returncode != 0 and "M2 contract status must be VERIFIED" in proc.stdout
    assert not (d / "must-not-exist.bin").exists()
    mutations = [
        ("sample_count", 1024),
        ("vector_len_data", 1024),
        ("arithmetic", "truncate"),
        ("validation", {**base["validation"], "binary_readback_bit_exact": False}),
        ("format", {**base["format"], "q_field": "[15:0] signed int16"}),
        ("files", [{**entry, "bytes": 8188} if entry["name"] == "m2_expected.bin" else entry
                   for entry in base["files"]]),
    ]
    for key, value in mutations:
        bad = dict(base); bad[key] = value
        manifest = d / f"bad-{key}.json"; manifest.write_text(json.dumps(bad))
        proc = subprocess.run(CLI + ["--validate-m2"] + common + ["--manifest", str(manifest)],
                              text=True, capture_output=True, timeout=15)
        assert proc.returncode != 0, (key, proc.stdout, proc.stderr)
        assert not (d / "must-not-exist.bin").exists()

print(f"M2 MATLAB reference audit PASS ({len(results)} cases): " + ", ".join(results))
print("Offline only: no m2_actual.bin created, no board or DMA access")
