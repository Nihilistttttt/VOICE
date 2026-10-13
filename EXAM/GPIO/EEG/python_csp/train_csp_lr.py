#!/usr/bin/env python3
"""
Train CSP + Linear SVM for 4-channel LEFT/RIGHT EEG intent recognition.

Input log format comes from firmware mode:
    #define EEG_APP_MODE EEG_APP_MODE_COLLECT_CSP

Expected serial lines:
    CSP_BEGIN,label=2,win=1,fs=250,n=256,ch=4,unit=uVx1000,order=CP3_CP4_C3_C4
    CSP,1,0,ch0,ch1,ch2,ch3
    ... 256 rows ...
    CSP_END,label=2,win=1

Labels used by firmware:
    LEFT  = 2
    RIGHT = 3

Default usage in PyCharm:
    1) Save your serial logs into python_csp/data/
    2) Run this file directly. It automatically reads all .txt/.log/.csv files in data/.

Output:
    Common/DualCore_CSP_Model_4ch_Weights.h
This header is compiled by V5F. When CSP4CH_WEIGHTS_TRAINED=1, V5F automatically uses CSP+SVM;
otherwise it falls back to the previous 24-dim FFT classifier.
"""
from __future__ import annotations

import argparse
import re
from pathlib import Path
from typing import Dict, List, Tuple

import numpy as np
from sklearn.metrics import accuracy_score, confusion_matrix
from sklearn.model_selection import StratifiedKFold, train_test_split
from sklearn.preprocessing import StandardScaler
from sklearn.svm import LinearSVC

LEFT_LABEL = 2
RIGHT_LABEL = 3
LABEL_TO_CLASS = {LEFT_LABEL: 0, RIGHT_LABEL: 1}  # MCU/Python: LEFT=0, RIGHT=1
CLASS_TO_NAME = {0: "LEFT", 1: "RIGHT"}

BEGIN_RE = re.compile(r"CSP_BEGIN,.*label=(\d+),win=(\d+).*(?:n=(\d+)).*")
ROW_RE = re.compile(r"CSP,(\d+),(\d+),(-?\d+),(-?\d+),(-?\d+),(-?\d+)")
END_RE = re.compile(r"CSP_END,.*label=(\d+),win=(\d+)")


def parse_csp_lines(lines: List[str], source_name: str = "<memory>", expected_n: int = 256) -> Tuple[List[np.ndarray], List[int]]:
    trials: List[np.ndarray] = []
    labels: List[int] = []

    cur_win = None
    cur_label = None
    rows: Dict[int, List[int]] = {}

    for raw in lines:
        line = raw.strip()
        if not line:
            continue

        m = BEGIN_RE.match(line)
        if m:
            cur_label = int(m.group(1))
            cur_win = int(m.group(2))
            rows = {}
            continue

        m = ROW_RE.match(line)
        if m and cur_win is not None:
            win = int(m.group(1))
            idx = int(m.group(2))
            if win != cur_win:
                continue
            rows[idx] = [int(m.group(3)), int(m.group(4)), int(m.group(5)), int(m.group(6))]
            continue

        m = END_RE.match(line)
        if m and cur_win is not None:
            label = int(m.group(1))
            win = int(m.group(2))
            if label == cur_label and win == cur_win and cur_label in LABEL_TO_CLASS:
                if len(rows) == expected_n and all(i in rows for i in range(expected_n)):
                    arr = np.asarray([rows[i] for i in range(expected_n)], dtype=np.float64).T
                    # Firmware unit is uVx1000. Convert to uV for numerically comfortable training.
                    arr = arr / 1000.0
                    # Remove residual per-channel DC before CSP covariance.
                    arr = arr - arr.mean(axis=1, keepdims=True)
                    trials.append(arr)
                    labels.append(LABEL_TO_CLASS[cur_label])
            cur_win = None
            cur_label = None
            rows = {}

    if not trials:
        raise RuntimeError(f"No valid CSP windows parsed from {source_name}")

    return trials, labels


def discover_data_files(data_dir: Path) -> List[Path]:
    """Find CSP log files in python_csp/data automatically.

    Put any serial-saved .txt/.log/.csv file in this folder. The trainer will
    read all matching files and merge valid CSP windows.
    """
    if not data_dir.exists():
        raise RuntimeError(f"Data folder does not exist: {data_dir}")

    patterns = ("*.txt", "*.log", "*.csv")
    files: List[Path] = []
    for pat in patterns:
        files.extend(data_dir.glob(pat))

    # Avoid accidentally reading output/report files if they are in data/.
    skip_names = {"train_report.txt", "README.txt"}
    files = [f for f in files if f.is_file() and f.name not in skip_names]
    files = sorted(set(files), key=lambda x: x.name.lower())
    if not files:
        raise RuntimeError(
            f"No data file found in {data_dir}. Put your serial log .txt/.log/.csv into this folder."
        )
    return files


def parse_csp_files(paths: List[Path], expected_n: int = 256) -> Tuple[np.ndarray, np.ndarray]:
    all_trials: List[np.ndarray] = []
    all_labels: List[int] = []

    for path in paths:
        lines = path.read_text(errors="ignore").splitlines()
        try:
            trials, labels = parse_csp_lines(lines, source_name=str(path), expected_n=expected_n)
        except RuntimeError as e:
            print(f"[WARN] {e}")
            continue
        all_trials.extend(trials)
        all_labels.extend(labels)
        print(f"[DATA] {path.name}: {len(labels)} valid CSP windows")

    if not all_trials:
        raise RuntimeError("No valid CSP windows parsed from selected data files.")

    X = np.stack(all_trials, axis=0)  # (trials, channels, samples)
    y = np.asarray(all_labels, dtype=np.int64)
    return X, y


def normalized_cov(trial: np.ndarray) -> np.ndarray:
    x = trial - trial.mean(axis=1, keepdims=True)
    cov = x @ x.T
    tr = np.trace(cov)
    if tr <= 1e-12:
        tr = 1.0
    return cov / tr


def fit_csp(X: np.ndarray, y: np.ndarray, n_components: int = 4) -> np.ndarray:
    if n_components != 4:
        raise ValueError("MCU side currently expects 4 CSP components")

    cov_l = np.mean([normalized_cov(x) for x in X[y == 0]], axis=0)
    cov_r = np.mean([normalized_cov(x) for x in X[y == 1]], axis=0)
    cov_sum = cov_l + cov_r

    # Solve inv(Rl+Rr)*Rl. Eigenvectors at both ends maximize variance for opposite classes.
    vals, vecs = np.linalg.eig(np.linalg.pinv(cov_sum) @ cov_l)
    vals = np.real(vals)
    vecs = np.real(vecs)
    order = np.argsort(vals)[::-1]
    selected = [order[0], order[1], order[-2], order[-1]]
    W = vecs[:, selected].T

    # Normalize each spatial filter for stable exported numbers.
    for i in range(W.shape[0]):
        norm = np.linalg.norm(W[i])
        if norm > 1e-12:
            W[i] /= norm
    return W.astype(np.float64)


def csp_features(X: np.ndarray, W: np.ndarray) -> np.ndarray:
    feats = []
    for trial in X:
        z = W @ trial
        var = np.var(z, axis=1)
        var = np.maximum(var, 1e-18)
        feat = np.log(var / np.sum(var))
        feats.append(feat)
    return np.asarray(feats, dtype=np.float64)


def fit_model(X: np.ndarray, y: np.ndarray):
    W = fit_csp(X, y, n_components=4)
    F = csp_features(X, W)
    scaler = StandardScaler()
    Fs = scaler.fit_transform(F)
    clf = LinearSVC(C=1.0, class_weight="balanced", max_iter=20000, random_state=7)
    clf.fit(Fs, y)
    return W, scaler, clf, F


def evaluate(X: np.ndarray, y: np.ndarray, folds: int = 5) -> str:
    n_left = int(np.sum(y == 0))
    n_right = int(np.sum(y == 1))
    min_class = min(n_left, n_right)
    if min_class < 2:
        return "Cross-validation skipped: each class needs at least 2 windows."

    k = min(folds, min_class)
    skf = StratifiedKFold(n_splits=k, shuffle=True, random_state=7)
    preds = np.zeros_like(y)
    for train_idx, test_idx in skf.split(X, y):
        W, scaler, clf, _ = fit_model(X[train_idx], y[train_idx])
        F_test = csp_features(X[test_idx], W)
        preds[test_idx] = clf.predict(scaler.transform(F_test))

    acc = accuracy_score(y, preds)
    cm = confusion_matrix(y, preds, labels=[0, 1])
    return (
        f"Cross-val folds: {k}\n"
        f"Cross-val accuracy: {acc:.4f}\n"
        f"Confusion matrix rows=true LEFT/RIGHT, cols=pred LEFT/RIGHT:\n{cm}\n"
    )


def c_array_1d(name: str, arr: np.ndarray) -> str:
    items = ", ".join(f"{float(v):.9e}f" for v in arr.ravel())
    return f"static const float {name}[{arr.size}] = {{\n    {items}\n}};\n"


def c_array_2d(name: str, arr: np.ndarray) -> str:
    rows = []
    for r in arr:
        rows.append("    {" + ", ".join(f"{float(v):.9e}f" for v in r) + "}")
    return f"static const float {name}[{arr.shape[0]}][{arr.shape[1]}] = {{\n" + ",\n".join(rows) + "\n};\n"


def export_header(out_path: Path, W: np.ndarray, scaler: StandardScaler, clf: LinearSVC):
    out_path.parent.mkdir(parents=True, exist_ok=True)
    coef = clf.coef_[0].astype(np.float64)
    bias = float(clf.intercept_[0])
    text = """#ifndef __DUALCORE_CSP_MODEL_4CH_WEIGHTS_H\n#define __DUALCORE_CSP_MODEL_4CH_WEIGHTS_H\n\n/* Auto-generated by python_csp/train_csp_lr.py.\n * Firmware labels: LEFT=2, RIGHT=3. Model classes: LEFT=0, RIGHT=1.\n * Input channels: CP3, CP4, C3, C4. Input unit during training: uV.\n */\n#define CSP4CH_INPUT_CH          4\n#define CSP4CH_FEATURE_DIM       4\n#define CSP4CH_WEIGHTS_TRAINED   1\n\n"""
    text += c_array_2d("csp4ch_matrix", W)
    text += "\n"
    text += c_array_1d("csp4ch_mean", scaler.mean_.astype(np.float64))
    text += "\n"
    text += c_array_1d("csp4ch_scale", scaler.scale_.astype(np.float64))
    text += "\n"
    text += c_array_1d("csp4ch_svm_weight", coef)
    text += f"\nstatic const float csp4ch_svm_bias = {bias:.9e}f;\n\n"
    text += "#endif /* __DUALCORE_CSP_MODEL_4CH_WEIGHTS_H */\n"
    out_path.write_text(text, encoding="utf-8")


def main():
    root = Path(__file__).resolve().parents[1]
    script_dir = Path(__file__).resolve().parent
    parser = argparse.ArgumentParser()
    parser.add_argument("--data-dir", type=Path, default=script_dir / "data",
                        help="Folder containing serial-saved CSP logs. Default: python_csp/data")
    parser.add_argument("--log", type=Path, default=None,
                        help="Optional: train from one specified log file. If omitted, all .txt/.log/.csv files in data-dir are used.")
    parser.add_argument("--out", type=Path, default=root / "Common" / "DualCore_CSP_Model_4ch_Weights.h",
                        help="Output header copied into firmware Common folder")
    parser.add_argument("--report", type=Path, default=script_dir / "train_report.txt")
    args = parser.parse_args()

    if args.log is not None:
        data_files = [args.log]
    else:
        data_files = discover_data_files(args.data_dir)

    print("Using data files:")
    for f in data_files:
        print(f"  - {f}")

    X, y = parse_csp_files(data_files)
    if len(np.unique(y)) != 2:
        raise RuntimeError("Training requires both LEFT(label=2) and RIGHT(label=3) windows.")

    report = []
    report.append("Data files:")
    report.extend([f"  - {str(f)}" for f in data_files])
    report.append("")
    report.append(f"Loaded windows: {len(y)}")
    report.append(f"LEFT windows: {int(np.sum(y == 0))}")
    report.append(f"RIGHT windows: {int(np.sum(y == 1))}")
    report.append(f"Shape: {X.shape}  # trials, channels, samples")
    report.append("")
    report.append(evaluate(X, y))

    W, scaler, clf, F = fit_model(X, y)
    pred = clf.predict(scaler.transform(F))
    train_acc = accuracy_score(y, pred)
    cm_train = confusion_matrix(y, pred, labels=[0, 1])
    report.append(f"Train accuracy: {train_acc:.4f}")
    report.append(f"Train confusion matrix:\n{cm_train}")
    report.append("")
    report.append("Exported header:")
    report.append(str(args.out))

    export_header(args.out, W, scaler, clf)
    args.report.write_text("\n".join(report), encoding="utf-8")
    print("\n".join(report))


if __name__ == "__main__":
    main()
