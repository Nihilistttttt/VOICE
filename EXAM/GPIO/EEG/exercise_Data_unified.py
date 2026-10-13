#!/usr/bin/env python3
# -*- coding: utf-8 -*-

"""
EEG 统一上位机 - AA55 二进制协议
- 串口/无线切换（顶部下拉框）
- MI 训练/测试（二进制命令收发）
- SSVEP 刺激 + FBCCA 评估（PyQt5 重写，去掉 pygame）
- 协议与 MCU eeg_protocol.h / Java EegProtocol.java 对齐
"""

import sys
import re
import csv
import struct
import socket
import subprocess
import time
import math
import random
import json
from datetime import datetime
from pathlib import Path
from collections import deque, Counter

import numpy as np
from scipy.signal import butter, filtfilt
import serial
import serial.tools.list_ports
from PyQt5.QtWidgets import (
    QApplication, QMainWindow, QWidget, QVBoxLayout, QHBoxLayout,
    QPushButton, QTextEdit, QLabel, QComboBox, QGroupBox,
    QProgressBar, QMessageBox, QLineEdit, QPlainTextEdit, QTabWidget,
    QStackedWidget, QFrame, QFileDialog, QSpinBox, QSlider, QCheckBox
)
from PyQt5.QtCore import QThread, pyqtSignal, Qt, QTimer
from PyQt5.QtGui import QFont, QIcon, QTextCursor, QPainter, QColor

import matplotlib
matplotlib.use('Qt5Agg')
import matplotlib.pyplot as plt
from matplotlib.backends.backend_qt5agg import FigureCanvasQTAgg as FigureCanvas
from matplotlib.figure import Figure
plt.rcParams['font.sans-serif'] = ['SimHei', 'Microsoft YaHei', 'DejaVu Sans']
plt.rcParams['axes.unicode_minus'] = False

# ============================================================
# AA55 二进制协议常量
# ============================================================

FRAME_HEADER0 = 0xAA
FRAME_HEADER1 = 0x55
FRAME_TAIL = 0x7E
ESCAPE_CHAR = 0x7D
ESCAPE_XOR = 0x20

ADDR_MCU = 0x01
ADDR_DOCTOR = 0x02
ADDR_PATIENT = 0x03
ADDR_PYTHON = 0x04

CMD_NULL = 0x00
CMD_MODE_SET = 0x01
CMD_MODE_TRAIN = 0x02
CMD_MODE_TEST = 0x03
CMD_MODEL_SET = 0x04
CMD_MODEL_GET = 0x05
CMD_TRIAL = 0x06
CMD_STOP = 0x07
CMD_STATUS = 0x08
CMD_IPCDIAG = 0x09
CMD_POSTURE = 0x0A
CMD_DISPLAY_CFG = 0x0B
CMD_SSVEP_START = 0x0C
CMD_SSVEP_STOP = 0x0D
CMD_ACK = 0x10

CMD_WAVE = 0x20
CMD_SPECTRUM = 0x21
CMD_FOCUS = 0x22
CMD_RESULT_MI = 0x23
CMD_RESULT_SSVEP = 0x24
CMD_DIAG = 0x25
CMD_DIRCSV = 0x26
CMD_CSP = 0x27
CMD_TASK = 0x28
CMD_INTENT = 0x29
CMD_EVENT = 0x2C

CMD_READY_TRAIN = 0x37
CMD_READY_TEST = 0x39
CMD_TASK_START = 0x3A
CMD_TASK_DONE = 0x3B
CMD_TASK_STOPPED = 0x3C
CMD_MODE_SET_OK = 0x3D
CMD_ALARM_ACK = 0x47
CMD_RECORD_START = 0x48
CMD_RECORD_STOP = 0x49

WAVE_NUM_CH = 8
WAVE_PAYLOAD = 1 + WAVE_NUM_CH * 4

FORWARD_PORT = 41003
CMD_PORT = 41003
BAUDRATE = 2000000

CMD_NAMES = {
    CMD_MODE_SET: "MODE_SET", CMD_MODE_TRAIN: "MODE_TRAIN", CMD_MODE_TEST: "MODE_TEST",
    CMD_MODEL_SET: "MODEL_SET", CMD_MODEL_GET: "MODEL_GET", CMD_TRIAL: "TRIAL",
    CMD_STOP: "STOP", CMD_STATUS: "STATUS", CMD_IPCDIAG: "IPCDIAG", CMD_POSTURE: "POSTURE",
    CMD_DISPLAY_CFG: "DISPLAY_CFG", CMD_SSVEP_START: "SSVEP_START", CMD_SSVEP_STOP: "SSVEP_STOP",
    CMD_ACK: "ACK", CMD_WAVE: "WAVE", CMD_SPECTRUM: "SPECTRUM", CMD_FOCUS: "FOCUS",
    CMD_RESULT_MI: "RESULT_MI", CMD_RESULT_SSVEP: "RESULT_SSVEP", CMD_DIAG: "DIAG",
    CMD_DIRCSV: "DIRCSV", CMD_CSP: "CSP", CMD_TASK: "TASK", CMD_INTENT: "INTENT",
    CMD_EVENT: "EVENT", CMD_READY_TRAIN: "READY_TRAIN", CMD_READY_TEST: "READY_TEST",
    CMD_TASK_START: "TASK_START", CMD_TASK_DONE: "TASK_DONE", CMD_TASK_STOPPED: "TASK_STOPPED",
    CMD_MODE_SET_OK: "MODE_SET_OK", CMD_ALARM_ACK: "ALARM_ACK",
    CMD_RECORD_START: "RECORD_START", CMD_RECORD_STOP: "RECORD_STOP",
}

# ============================================================
# 帧打包 / 解包
# ============================================================

def checksum16(data):
    return sum(data) & 0xFFFF


def pack_frame(addr, cmd, payload=b''):
    """打包 AA55 二进制帧（含转义）。payload = [cmd][data...]（下行）或纯 data（上行打包时）。"""
    pay_len = len(payload)
    ts = int(time.time() * 1000) & 0xFFFFFFFF
    body = bytearray()
    body.append(addr)
    body.append(cmd)
    body += struct.pack('<H', pay_len)
    body += struct.pack('<I', ts)
    body += payload
    crc = checksum16(body)
    body += struct.pack('<H', crc)

    frame = bytearray()
    frame.append(FRAME_HEADER0)
    frame.append(FRAME_HEADER1)
    for b in body:
        if b in (FRAME_HEADER0, FRAME_HEADER1, FRAME_TAIL, ESCAPE_CHAR):
            frame.append(ESCAPE_CHAR)
            frame.append(b ^ ESCAPE_XOR)
        else:
            frame.append(b)
    frame.append(FRAME_TAIL)
    return bytes(frame)


def build_downstream_payload(cmd, data=b''):
    """下行命令载荷 = [cmd][data...]，MCU Parse_CommandBinary 解析 payload 首字节。"""
    return bytes([cmd]) + data


class FrameParser:
    """AA55 帧状态机解析器（上行帧），同时提取非帧文本日志。"""

    S_WAIT_AA = 0
    S_WAIT_55 = 1
    S_BODY = 2
    S_ESCAPE = 3

    def __init__(self, callback, text_cb=None):
        self.callback = callback
        self.text_cb = text_cb
        self.state = self.S_WAIT_AA
        self.body = bytearray()
        self._max = 2048
        self._text = bytearray()

    def feed(self, raw_bytes):
        for b in raw_bytes:
            self._feed_byte(b)

    def _flush_text(self):
        if self.text_cb and len(self._text) > 0:
            line = self._text.decode('ascii', errors='replace').rstrip('\r\n')
            if line:
                self.text_cb(line)
        self._text = bytearray()

    def _feed_byte(self, b):
        if self.state == self.S_WAIT_AA:
            if b == FRAME_HEADER0:
                self._flush_text()
                self.state = self.S_WAIT_55
            else:
                self._text.append(b)
                if b == 0x0A:
                    self._flush_text()
        elif self.state == self.S_WAIT_55:
            if b == FRAME_HEADER1:
                self.state = self.S_BODY
                self.body = bytearray()
            elif b == FRAME_HEADER0:
                pass
            else:
                self.state = self.S_WAIT_AA
        elif self.state == self.S_BODY:
            if b == ESCAPE_CHAR:
                self.state = self.S_ESCAPE
            elif b == FRAME_TAIL:
                if len(self.body) >= 10:
                    self._dispatch()
                self.state = self.S_WAIT_AA
                self.body = bytearray()
            else:
                if len(self.body) < self._max:
                    self.body.append(b)
                else:
                    self.state = self.S_WAIT_AA
        elif self.state == self.S_ESCAPE:
            if len(self.body) < self._max:
                self.body.append(b ^ ESCAPE_XOR)
            self.state = self.S_BODY

    def _dispatch(self):
        body = self.body
        fixed = 8
        if len(body) < fixed + 2:
            return
        addr = body[0]
        cmd = body[1]
        pay_len = struct.unpack_from('<H', body, 2)[0]
        if len(body) != fixed + pay_len + 2:
            return
        calc = checksum16(body[:fixed + pay_len])
        recv_crc = struct.unpack_from('<H', body, fixed + pay_len)[0]
        if calc != recv_crc:
            return
        payload = bytes(body[fixed:fixed + pay_len])
        self.callback(cmd, payload)


# ============================================================
# 自动获取默认网关
# ============================================================

def get_default_gateway():
    system = sys.platform
    try:
        if system.startswith('win'):
            out = subprocess.check_output("route print -4", shell=True, text=True)
            for line in out.splitlines():
                if '0.0.0.0' in line and '224.0.0.0' not in line:
                    parts = line.split()
                    if len(parts) >= 3:
                        gw = parts[2]
                        if gw != '0.0.0.0':
                            return gw
        else:
            out = subprocess.check_output("ip route show default", shell=True, text=True)
            m = re.search(r'default via ([\d.]+)', out)
            if m:
                return m.group(1)
    except Exception:
        pass
    return "192.168.43.1"


# ============================================================
# FBCCA 信号处理 + 算法（复用自 SSVEP.py）
# ============================================================

SAMPLE_RATE = 250.0
NOTCH_FREQ = 50.0
RHO = 0.98
MA_WINDOW = 3

TARGET_FREQS = [11.0, 13.0, 15.0, 17.0]

FBCCA_WINDOW_SEC = 5.0
FBCCA_WINDOW_SIZE = int(SAMPLE_RATE * FBCCA_WINDOW_SEC)
FBCCA_STEP_SEC = 2.0
FBCCA_STEP_SIZE = int(SAMPLE_RATE * FBCCA_STEP_SEC)
FBCCA_HARMONICS = 2
FBCCA_USE_CHANNELS = [0]

FILTER_BANKS = [
    (6.0, 45.0),
    (10.0, 45.0),
    (14.0, 45.0),
    (18.0, 45.0),
]

ENABLE_FREQ_TOLERANCE_SEARCH = True
FREQ_SEARCH_OFFSETS = [-0.05, 0.0, 0.05]

ENABLE_TIME_WEIGHTED_CCA = True
TIME_WEIGHT_MODE = "tukey"
TIME_WEIGHT_TUKEY_ALPHA = 0.25
TIME_WEIGHT_RECENT_MIN = 0.70
TIME_WEIGHT_RECENT_MAX = 1.10

FBCCA_RATIO_THRESHOLD = 1.25
FBCCA_MARGIN_THRESHOLD = 0.01
FBCCA_MIN_SCORE = 0.00

REFERENCE_CACHE = {}

ENABLE_VOTE = True
VOTE_HISTORY_LEN = 5
VOTE_MIN_COUNT = 3

ENABLE_CHANNEL_QUALITY_CHECK = True
FBCCA_MIN_VALID_CHANNELS = 1
CHANNEL_MIN_STD_MV = 1e-7
CHANNEL_MAX_STD_MV = 100.0
CHANNEL_MAX_PTP_MV = 300.0

SSVEP_CHANNEL = 1

_w0 = 2 * np.pi * NOTCH_FREQ / SAMPLE_RATE
_cos_w0 = np.cos(_w0)
_b_notch = [1, -2 * _cos_w0, 1]
_a_notch = [1, -2 * RHO * _cos_w0, RHO ** 2]


class NotchFilter:
    def __init__(self):
        self.x_state = [0.0, 0.0]
        self.y_state = [0.0, 0.0]

    def process(self, x):
        y = (
            _b_notch[0] * x
            + _b_notch[1] * self.x_state[0]
            + _b_notch[2] * self.x_state[1]
            - _a_notch[1] * self.y_state[0]
            - _a_notch[2] * self.y_state[1]
        )
        self.x_state[1] = self.x_state[0]
        self.x_state[0] = x
        self.y_state[1] = self.y_state[0]
        self.y_state[0] = y
        return y


def _moving_average(samples, new_val):
    samples.append(new_val)
    return np.mean(samples)


class DisplayFilter:
    def __init__(self):
        self.notch = NotchFilter()
        self.ma_samples = deque(maxlen=MA_WINDOW)

    def process(self, x):
        y_notch = self.notch.process(x)
        return _moving_average(self.ma_samples, y_notch)


def make_fbcca_weights(num_banks):
    weights = []
    for i in range(num_banks):
        m = i + 1
        weights.append(m ** (-1.25) + 0.25)
    return np.array(weights, dtype=float)


def generate_reference_signals(freq, sample_rate, n_samples, n_harmonics):
    t = np.arange(n_samples) / sample_rate
    refs = []
    nyquist = sample_rate / 2.0
    for h in range(1, n_harmonics + 1):
        hf = h * freq
        if hf >= nyquist:
            continue
        refs.append(np.sin(2 * np.pi * hf * t))
        refs.append(np.cos(2 * np.pi * hf * t))
    if not refs:
        return None
    return np.array(refs, dtype=float)


def get_reference_signals_cached(freq, sample_rate, n_samples, n_harmonics):
    key = (round(float(freq), 6), round(float(sample_rate), 6), int(n_samples), int(n_harmonics))
    if key in REFERENCE_CACHE:
        return REFERENCE_CACHE[key]
    ref = generate_reference_signals(freq, sample_rate, n_samples, n_harmonics)
    REFERENCE_CACHE[key] = ref
    return ref


def inv_sqrtm_psd(matrix, eps=1e-8):
    eigenvalues, eigenvectors = np.linalg.eigh(matrix)
    eigenvalues = np.maximum(eigenvalues, eps)
    inv_sqrt_values = 1.0 / np.sqrt(eigenvalues)
    return eigenvectors @ np.diag(inv_sqrt_values) @ eigenvectors.T


def tukey_window(n_samples, alpha=0.25):
    n_samples = int(n_samples)
    if n_samples <= 0:
        return np.array([], dtype=float)
    if n_samples == 1:
        return np.ones(1, dtype=float)
    alpha = float(np.clip(alpha, 0.0, 1.0))
    if alpha <= 0.0:
        return np.ones(n_samples, dtype=float)
    if alpha >= 1.0:
        return np.hanning(n_samples)
    n = np.arange(n_samples, dtype=float)
    w = np.ones(n_samples, dtype=float)
    edge = alpha * (n_samples - 1) / 2.0
    left = n < edge
    right = n >= (n_samples - 1) * (1.0 - alpha / 2.0)
    if np.any(left):
        w[left] = 0.5 * (1.0 + np.cos(np.pi * (2.0 * n[left] / (alpha * (n_samples - 1)) - 1.0)))
    if np.any(right):
        w[right] = 0.5 * (1.0 + np.cos(np.pi * (2.0 * n[right] / (alpha * (n_samples - 1)) - 2.0 / alpha + 1.0)))
    return w


def make_time_weights(n_samples):
    if not ENABLE_TIME_WEIGHTED_CCA:
        return None
    n_samples = int(n_samples)
    if n_samples <= 0:
        return None
    mode = str(TIME_WEIGHT_MODE).lower().strip()
    if mode in ("none", "flat", "rect", "rectangle"):
        w = np.ones(n_samples, dtype=float)
    elif mode == "hann":
        w = np.hanning(n_samples)
    elif mode == "tukey":
        w = tukey_window(n_samples, alpha=TIME_WEIGHT_TUKEY_ALPHA)
    elif mode == "recent_linear":
        w = np.linspace(TIME_WEIGHT_RECENT_MIN, TIME_WEIGHT_RECENT_MAX, n_samples, dtype=float)
    elif mode == "tukey_recent":
        base = tukey_window(n_samples, alpha=TIME_WEIGHT_TUKEY_ALPHA)
        recent = np.linspace(TIME_WEIGHT_RECENT_MIN, TIME_WEIGHT_RECENT_MAX, n_samples, dtype=float)
        w = base * recent
    else:
        w = np.ones(n_samples, dtype=float)
    w = np.asarray(w, dtype=float)
    w[~np.isfinite(w)] = 0.0
    w = np.maximum(w, 1e-8)
    mean_w = np.mean(w)
    if mean_w <= 0:
        return None
    return w / mean_w


def weighted_center(X, sample_weights):
    if sample_weights is None:
        return X - np.mean(X, axis=1, keepdims=True)
    w = np.asarray(sample_weights, dtype=float)
    w = w / (np.sum(w) + 1e-12)
    mean = X @ w
    return X - mean[:, np.newaxis]


def cca_corrcoef(X, Y, reg=1e-6, sample_weights=None):
    X = np.asarray(X, dtype=float)
    Y = np.asarray(Y, dtype=float)
    if X.ndim != 2 or Y.ndim != 2 or X.shape[1] != Y.shape[1]:
        return 0.0
    n_samples = X.shape[1]
    if n_samples < 5:
        return 0.0
    if sample_weights is None:
        X = X - np.mean(X, axis=1, keepdims=True)
        Y = Y - np.mean(Y, axis=1, keepdims=True)
        Cxx = (X @ X.T) / (n_samples - 1)
        Cyy = (Y @ Y.T) / (n_samples - 1)
        Cxy = (X @ Y.T) / (n_samples - 1)
    else:
        w = np.asarray(sample_weights, dtype=float).reshape(-1)
        if w.shape[0] != n_samples or not np.all(np.isfinite(w)):
            return 0.0
        w = np.maximum(w, 0.0)
        weight_sum = np.sum(w)
        if weight_sum <= 1e-12:
            return 0.0
        w = w / weight_sum
        X = weighted_center(X, w)
        Y = weighted_center(Y, w)
        sqrt_w = np.sqrt(w)[np.newaxis, :]
        Xw = X * sqrt_w
        Yw = Y * sqrt_w
        Cxx = Xw @ Xw.T
        Cyy = Yw @ Yw.T
        Cxy = Xw @ Yw.T
    Cxx += reg * np.eye(Cxx.shape[0])
    Cyy += reg * np.eye(Cyy.shape[0])
    Cxx_inv_sqrt = inv_sqrtm_psd(Cxx)
    Cyy_inv_sqrt = inv_sqrtm_psd(Cyy)
    M = Cxx_inv_sqrt @ Cxy @ Cyy_inv_sqrt
    singular_values = np.linalg.svd(M, compute_uv=False)
    if len(singular_values) == 0:
        return 0.0
    return float(np.clip(singular_values[0], 0.0, 1.0))


def fft_bandpass_eeg(eeg_data, sample_rate, low_freq, high_freq):
    eeg_data = np.asarray(eeg_data, dtype=float)
    n_samples = eeg_data.shape[1]
    freqs = np.fft.rfftfreq(n_samples, d=1.0 / sample_rate)
    spectrum = np.fft.rfft(eeg_data, axis=1)
    mask = (freqs >= low_freq) & (freqs <= high_freq)
    spectrum = spectrum * mask[np.newaxis, :]
    return np.fft.irfft(spectrum, n=n_samples, axis=1)


def preprocess_eeg_for_fbcca(eeg_data, sample_rate, low_freq, high_freq):
    X = np.asarray(eeg_data, dtype=float)
    X = X - np.mean(X, axis=1, keepdims=True)
    X = fft_bandpass_eeg(X, sample_rate, low_freq, high_freq)
    X = X - np.mean(X, axis=1, keepdims=True)
    std = np.std(X, axis=1, keepdims=True)
    return X / (std + 1e-8)


def fbcca_predict(eeg_data, target_freqs, sample_rate, n_harmonics, filter_banks):
    eeg_data = np.asarray(eeg_data, dtype=float)
    n_samples = eeg_data.shape[1]
    weights = make_fbcca_weights(len(filter_banks))
    sample_weights = make_time_weights(n_samples)

    preprocessed_banks = []
    for low_freq, high_freq in filter_banks:
        preprocessed_banks.append(preprocess_eeg_for_fbcca(eeg_data, sample_rate, low_freq, high_freq))

    scores = {}
    search_offsets = FREQ_SEARCH_OFFSETS if ENABLE_FREQ_TOLERANCE_SEARCH else [0.0]

    for target_freq in target_freqs:
        best_total_score = -1.0
        for offset in search_offsets:
            ref_freq = target_freq + offset
            if ref_freq <= 0:
                continue
            total_score = 0.0
            for bank_idx, (low_freq, high_freq) in enumerate(filter_banks):
                X_bank = preprocessed_banks[bank_idx]
                reference = get_reference_signals_cached(ref_freq, sample_rate, n_samples, n_harmonics)
                if reference is None:
                    rho = 0.0
                else:
                    rho = cca_corrcoef(X_bank, reference, sample_weights=sample_weights)
                total_score += weights[bank_idx] * (rho ** 2)
            if total_score > best_total_score:
                best_total_score = total_score
        scores[target_freq] = float(max(best_total_score, 0.0))

    predicted_freq = max(scores, key=scores.get)
    return predicted_freq, scores


def choose_fbcca_result(scores, ratio_threshold, min_score=0.0, margin_threshold=0.0):
    if not scores:
        return None, 0.0, 0.0, 0.0
    sorted_items = sorted(scores.items(), key=lambda item: item[1], reverse=True)
    best_freq, best_score = sorted_items[0]
    if best_score < min_score:
        return None, 0.0, best_score, 0.0
    if len(sorted_items) == 1:
        return best_freq, 999.0, best_score, best_score
    second_freq, second_score = sorted_items[1]
    ratio = best_score / (second_score + 1e-12)
    margin = best_score - second_score
    if ratio < ratio_threshold or margin < margin_threshold:
        return None, ratio, best_score, margin
    return best_freq, ratio, best_score, margin


def is_channel_usable(x):
    x = np.asarray(x, dtype=float)
    if len(x) < 10 or not np.all(np.isfinite(x)):
        return False
    std = np.std(x)
    ptp = np.max(x) - np.min(x)
    if std < CHANNEL_MIN_STD_MV or std > CHANNEL_MAX_STD_MV or ptp > CHANNEL_MAX_PTP_MV:
        return False
    return True


VOTE_HISTORY = deque(maxlen=VOTE_HISTORY_LEN)


def vote_result(new_result):
    if not ENABLE_VOTE:
        return new_result, Counter()
    if new_result is None:
        VOTE_HISTORY.clear()
        return None, Counter()
    VOTE_HISTORY.append(new_result)
    if len(VOTE_HISTORY) < VOTE_MIN_COUNT:
        return None, Counter(VOTE_HISTORY)
    counter = Counter(VOTE_HISTORY)
    if not counter:
        return None, counter
    best_freq, count = counter.most_common(1)[0]
    if count >= VOTE_MIN_COUNT:
        return best_freq, counter
    return None, counter


# ============================================================
# 连接抽象层
# ============================================================

class SerialConnection(QThread):
    """串口连接线程，接收二进制字节送入 FrameParser。"""
    frame_received = pyqtSignal(int, bytes)
    text_received = pyqtSignal(str)
    status_signal = pyqtSignal(str)

    def __init__(self, port, baudrate=BAUDRATE):
        super().__init__()
        self.port = port
        self.baudrate = baudrate
        self.serial = None
        self.running = False
        self.parser = FrameParser(self._on_frame, self._on_text)

    def _on_frame(self, cmd, payload):
        self.frame_received.emit(cmd, payload)

    def _on_text(self, line):
        self.text_received.emit(line)

    def run(self):
        try:
            self.serial = serial.Serial(self.port, self.baudrate, timeout=0.1)
            self.running = True
            self.status_signal.emit("OK")
            while self.running:
                if self.serial.in_waiting:
                    data = self.serial.read(self.serial.in_waiting)
                    if data:
                        self.parser.feed(data)
                else:
                    self.msleep(5)
        except Exception as e:
            self.status_signal.emit(f"ERROR:{str(e)}")
        finally:
            if self.serial and self.serial.is_open:
                self.serial.close()

    def stop(self):
        self.running = False
        self.quit()
        self.wait()

    def send_frame(self, cmd, data=b''):
        if self.serial and self.serial.is_open:
            payload = build_downstream_payload(cmd, data)
            self.serial.write(pack_frame(ADDR_PYTHON, cmd, payload))


class TcpConnection(QThread):
    """无线 TCP 连接线程，连接 Android 中转 41003。"""
    frame_received = pyqtSignal(int, bytes)
    status_signal = pyqtSignal(str)

    def __init__(self, ip, data_port=FORWARD_PORT, cmd_port=CMD_PORT):
        super().__init__()
        self.ip = ip
        self.data_port = data_port
        self.cmd_port = cmd_port
        self.data_sock = None
        self.cmd_sock = None
        self.running = False
        self.parser = FrameParser(self._on_frame)
        self._cmd_broken = False

    def _on_frame(self, cmd, payload):
        self.frame_received.emit(cmd, payload)

    def run(self):
        self.running = True
        while self.running:
            try:
                self.data_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.data_sock.settimeout(5)
                self.data_sock.connect((self.ip, self.data_port))
                self.data_sock.settimeout(0.1)

                self.cmd_sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
                self.cmd_sock.settimeout(5)
                self.cmd_sock.connect((self.ip, self.cmd_port))
                self.cmd_sock.settimeout(2)
                self._cmd_broken = False

                self.status_signal.emit("OK")
                self.parser = FrameParser(self._on_frame)

                while self.running:
                    try:
                        chunk = self.data_sock.recv(4096)
                        if not chunk:
                            break
                        self.parser.feed(chunk)
                    except socket.timeout:
                        continue
                    except Exception:
                        break
            except Exception as e:
                if self.running:
                    self.status_signal.emit(f"ERROR:{str(e)}")
            finally:
                for s in (self.data_sock, self.cmd_sock):
                    if s:
                        try:
                            s.close()
                        except Exception:
                            pass
                self.data_sock = None
                self.cmd_sock = None
            if self.running:
                time.sleep(3)

    def stop(self):
        self.running = False
        for s in (self.data_sock, self.cmd_sock):
            if s:
                try:
                    s.close()
                except Exception:
                    pass
        self.quit()
        self.wait()

    def send_frame(self, cmd, data=b''):
        if self._cmd_broken or not self.cmd_sock:
            return
        try:
            payload = build_downstream_payload(cmd, data)
            self.cmd_sock.sendall(pack_frame(ADDR_PYTHON, cmd, payload))
        except Exception:
            self._cmd_broken = True


# ============================================================
# SSVEP 刺激 Widget（PyQt5 重写，去掉 pygame）
# ============================================================

class SsvepStimWidget(QWidget):
    """黑白闪烁刺激 widget，用 QTimer + QPainter 实现。"""
    freq_changed = pyqtSignal(float)

    def __init__(self, parent=None):
        super().__init__(parent)
        self.setMinimumSize(400, 300)
        self.freq_index = 0
        self.stim_freq = TARGET_FREQS[self.freq_index]
        self._bright = True
        self._stim_active = False
        self._timer = QTimer(self)
        self._timer.timeout.connect(self._toggle)
        self._interval_ms = int(500.0 / self.stim_freq)

    def start_stim(self):
        self._stim_active = True
        self._bright = True
        self._interval_ms = max(1, int(500.0 / self.stim_freq))
        self._timer.start(self._interval_ms)
        self.update()

    def stop_stim(self):
        self._stim_active = False
        self._timer.stop()
        self.update()

    def _toggle(self):
        self._bright = not self._bright
        self.update()

    def set_freq(self, freq):
        self.stim_freq = freq
        self._interval_ms = max(1, int(500.0 / freq))
        if self._stim_active:
            self._timer.setInterval(self._interval_ms)
        self.freq_changed.emit(freq)
        self.update()

    def next_freq(self):
        self.freq_index = (self.freq_index + 1) % len(TARGET_FREQS)
        self.set_freq(TARGET_FREQS[self.freq_index])
        self.freq_changed.emit(TARGET_FREQS[self.freq_index])

    def prev_freq(self):
        self.freq_index = (self.freq_index - 1) % len(TARGET_FREQS)
        self.set_freq(TARGET_FREQS[self.freq_index])
        self.freq_changed.emit(TARGET_FREQS[self.freq_index])

    def paintEvent(self, event):
        painter = QPainter(self)
        rect = self.rect()
        if self._stim_active:
            color = QColor(255, 255, 255) if self._bright else QColor(30, 30, 30)
        else:
            color = QColor(60, 60, 60)
        painter.fillRect(rect, color)

        text_color = QColor(0, 0, 0) if self._bright else QColor(255, 255, 255)
        if not self._stim_active:
            text_color = QColor(200, 200, 200)
        painter.setPen(text_color)
        font = QFont("Segoe UI", 24, QFont.Bold)
        painter.setFont(font)

        cx = rect.width() // 2
        cy = rect.height() // 2
        if self._stim_active:
            painter.drawText(rect, Qt.AlignCenter, f"刺激频率: {self.stim_freq:.2f} Hz")
        else:
            painter.drawText(rect, Qt.AlignCenter, "SSVEP 刺激未启动")

        font_small = QFont("Segoe UI", 9)
        painter.setFont(font_small)
        hint_color = QColor(150, 150, 150)
        painter.setPen(hint_color)
        painter.drawText(rect.adjusted(0, 0, -10, -5), Qt.AlignBottom | Qt.AlignRight,
                         "↑↓ 切换频率 | 点击启动/停止")
        painter.end()

    def mousePressEvent(self, event):
        if self._stim_active:
            self.stop_stim()
        else:
            self.start_stim()

    def keyPressEvent(self, event):
        if event.key() == Qt.Key_Up:
            self.prev_freq()
        elif event.key() == Qt.Key_Down:
            self.next_freq()
        elif event.key() == Qt.Key_Escape:
            self.stop_stim()
        else:
            super().keyPressEvent(event)


# ============================================================
# 主窗口
# ============================================================

class MainWindow(QMainWindow):
    def __init__(self):
        super().__init__()
        self.setWindowTitle("EEG 统一控制台 - AA55 二进制协议")
        self.setGeometry(120, 120, 1600, 900)
        self.setMinimumSize(1000, 800)

        self.train_data = []
        self.trial_sequence = []
        self.current_trial_index = 0
        self.is_training = False
        self.is_testing = False
        self.waiting_for_ready = False
        self.pending_train = False
        self.pending_test = False
        self.test_results = []
        self.received_feature_count = 0
        self._last_seq = -1

        self.posture_name = "UNKNOWN"
        self.posture_gx = 0
        self.posture_gy = 0
        self.posture_gz = 0
        self.posture_conf = 0
        self.posture_stable_ms = 0
        self.turn_count = 0
        self.fall_detected = False

        self.connection = None
        self.conn_mode = "serial"
        self.fbcca_source = "python"

        self.init_ui()
        self.apply_stylesheet()

        self.refresh_timer = QTimer()
        self.refresh_timer.timeout.connect(self.refresh_ports)
        self.refresh_timer.start(2000)

        self._init_ssvep_analysis()

    def _init_ssvep_analysis(self):
        self.ssvep_display_filter = DisplayFilter()
        self.ssvep_analysis_filter = NotchFilter()
        self.ssvep_buffer = deque(maxlen=FBCCA_WINDOW_SIZE)
        self.ssvep_since_last_update = 0
        self.ssvep_round_index = 0
        self.ssvep_result = None
        self.ssvep_scores = {}
        self.ssvep_ratio = 0.0
        self.ssvep_best_score = 0.0
        self.ssvep_margin = 0.0
        self.ssvep_vote_counter = Counter()
        self.ssvep_sample_count = 0

    # ----------------------- 样式 -----------------------
    def apply_stylesheet(self):
        style = """
        QMainWindow { background-color: #f0f2f5; }
        QGroupBox {
            font-weight: bold; border: 1px solid #d0d7de; border-radius: 8px;
            margin-top: 8px; padding-top: 12px; background-color: white;
        }
        QGroupBox::title {
            subcontrol-origin: margin; left: 12px; padding: 0 6px; color: #24292f;
        }
        QLabel { color: #24292f; font-size: 9pt; }
        QPushButton {
            background-color: #40739e; color: white; border: none; border-radius: 6px;
            padding: 6px 12px; font-weight: bold; font-size: 9pt;
        }
        QPushButton:hover { background-color: #487eb0; }
        QPushButton:pressed { background-color: #353b48; }
        QPushButton:disabled { background-color: #b2bec3; color: #dfe6e9; }
        QPushButton#trainBtn { background-color: #00a8ff; }
        QPushButton#trainBtn:hover { background-color: #0097e6; }
        QPushButton#testBtn { background-color: #e1b12c; }
        QPushButton#testBtn:hover { background-color: #fbc531; }
        QPushButton#stopBtn { background-color: #e84118; }
        QPushButton#stopBtn:hover { background-color: #c23616; }
        QPushButton#connectBtn { background-color: #4cd137; }
        QPushButton#connectBtn:hover { background-color: #44bd32; }
        QPushButton#ssvepBtn { background-color: #9c88ff; }
        QPushButton#ssvepBtn:hover { background-color: #8c7ae6; }
        QPushButton#applyModeBtn { background-color: #00a8ff; }
        QPushButton#applyModeBtn:hover { background-color: #0097e6; }
        QPushButton#statusBtn { background-color: #9c88ff; }
        QPushButton#statusBtn:hover { background-color: #8c7ae6; }
        QPushButton#ipcdiagBtn { background-color: #4cd137; }
        QPushButton#ipcdiagBtn:hover { background-color: #44bd32; }
        QPushButton#ipcdiagBtn:checked { background-color: #7f8c8d; }
        QPushButton#postureBtn { background-color: #00cec9; }
        QPushButton#postureBtn:hover { background-color: #01b5b0; }
        QPushButton#postureBtn:checked { background-color: #7f8c8d; }
        QLineEdit, QComboBox, QPlainTextEdit, QTextEdit {
            border: 1px solid #d0d7de; border-radius: 6px; padding: 4px 8px;
            background-color: #f8f9fa; font-size: 9pt;
        }
        QLineEdit:focus, QComboBox:focus { border: 1px solid #40739e; background-color: white; }
        QProgressBar {
            border: 1px solid #d0d7de; border-radius: 6px; text-align: center;
            background-color: #ecf0f1; height: 18px; font-size: 9pt;
        }
        QProgressBar::chunk {
            background: qlineargradient(x1:0, y1:0, x2:1, y2:0, stop:0 #00a8ff, stop:1 #4cd137);
            border-radius: 6px;
        }
        QPlainTextEdit, QTextEdit {
            background-color: #fafbfc; font-family: "Consolas", "Courier New", monospace; font-size: 9pt;
        }
        QTabWidget::pane { border: 1px solid #d0d7de; border-radius: 6px; background-color: white; }
        QTabBar::tab {
            background-color: #e9ecef; border: 1px solid #d0d7de; border-bottom: none;
            border-top-left-radius: 6px; border-top-right-radius: 6px;
            padding: 4px 10px; margin-right: 2px; font-size: 9pt;
        }
        QTabBar::tab:selected { background-color: white; border-bottom: 2px solid #00a8ff; }
        QTabBar::tab:hover { background-color: #dee2e6; }
        QLabel#arrowLabel {
            background: qlineargradient(x1:0, y1:0, x2:0, y2:1, stop:0 #ffffff, stop:1 #f0f2f5);
            border: 2px solid #d0d7de; border-radius: 12px;
        }
        QFrame#stimFrame { border: 2px solid #d0d7de; border-radius: 8px; background-color: #2d3436; }
        """
        self.setStyleSheet(style)

    # ----------------------- UI -----------------------
    def init_ui(self):
        central = QWidget()
        self.setCentralWidget(central)
        main_layout = QVBoxLayout(central)
        main_layout.setSpacing(10)
        main_layout.setContentsMargins(16, 16, 16, 16)

        # ---- 1. 顶部连接区 ----
        conn_group = QGroupBox("连接控制")
        conn_layout = QHBoxLayout(conn_group)
        conn_layout.setSpacing(8)
        conn_layout.setContentsMargins(10, 12, 10, 10)

        conn_layout.addWidget(QLabel("连接方式:"))
        self.conn_mode_combo = QComboBox()
        self.conn_mode_combo.addItem("串口", "serial")
        self.conn_mode_combo.addItem("无线 (Android中转)", "tcp")
        self.conn_mode_combo.currentIndexChanged.connect(self.on_conn_mode_changed)
        conn_layout.addWidget(self.conn_mode_combo)

        self.conn_stack = QStackedWidget()

        serial_page = QWidget()
        sl = QHBoxLayout(serial_page)
        sl.setContentsMargins(0, 0, 0, 0)
        sl.addWidget(QLabel("端口:"))
        self.port_combo = QComboBox()
        self.port_combo.setMinimumWidth(100)
        sl.addWidget(self.port_combo)
        self.conn_stack.addWidget(serial_page)

        tcp_page = QWidget()
        tl = QHBoxLayout(tcp_page)
        tl.setContentsMargins(0, 0, 0, 0)
        tl.addWidget(QLabel("IP:"))
        self.ip_combo = QComboBox()
        self.ip_combo.setEditable(True)
        self.ip_combo.setMinimumWidth(130)
        self.ip_combo.setPlaceholderText("点击搜索或手动输入")
        tl.addWidget(self.ip_combo)
        self.discover_btn = QPushButton("获取网关")
        self.discover_btn.setObjectName("connectBtn")
        self.discover_btn.clicked.connect(self.discover_gateway)
        tl.addWidget(self.discover_btn)
        tl.addWidget(QLabel("端口:"))
        self.port_edit = QLineEdit(str(FORWARD_PORT))
        self.port_edit.setMaximumWidth(60)
        tl.addWidget(self.port_edit)
        self.conn_stack.addWidget(tcp_page)

        conn_layout.addWidget(self.conn_stack)

        self.connect_btn = QPushButton("连接")
        self.connect_btn.setObjectName("connectBtn")
        self.connect_btn.clicked.connect(self.toggle_connection)
        conn_layout.addWidget(self.connect_btn)
        self.status_label = QLabel("未连接")
        self.status_label.setStyleSheet("color: #e84118; font-weight: bold;")
        conn_layout.addWidget(self.status_label)
        conn_layout.addStretch()
        main_layout.addWidget(conn_group)

        # ---- 2. 模式 + 控制面板 ----
        ctrl_layout = QHBoxLayout()
        ctrl_layout.setSpacing(10)

        mode_group = QGroupBox("EEG 模式")
        mode_layout = QHBoxLayout(mode_group)
        mode_layout.setSpacing(8)
        mode_layout.setContentsMargins(10, 12, 10, 10)
        mode_layout.addWidget(QLabel("模式:"))
        self.mode_combo = QComboBox()
        self.mode_combo.addItem("COLLECT", 1)
        self.mode_combo.addItem("INFER", 2)
        mode_layout.addWidget(self.mode_combo)
        self.apply_mode_btn = QPushButton("应用")
        self.apply_mode_btn.setObjectName("applyModeBtn")
        self.apply_mode_btn.clicked.connect(self.apply_mode)
        self.apply_mode_btn.setEnabled(False)
        mode_layout.addWidget(self.apply_mode_btn)
        self.mode_status_label = QLabel("COLLECT")
        self.mode_status_label.setStyleSheet("color: #00a8ff; font-weight: bold;")
        mode_layout.addWidget(self.mode_status_label)
        mode_layout.addStretch()
        ctrl_layout.addWidget(mode_group, stretch=1)

        control_group = QGroupBox("控制面板")
        control_layout = QVBoxLayout(control_group)
        control_layout.setSpacing(6)
        control_layout.setContentsMargins(10, 12, 10, 10)

        status_row = QHBoxLayout()
        self.mode_label = QLabel("空闲")
        self.mode_label.setFont(QFont("Segoe UI", 9, QFont.Bold))
        self.mode_label.setStyleSheet("color: #7f8c8d;")
        status_row.addWidget(self.mode_label)
        self.trial_info = QLabel("")
        status_row.addStretch()
        control_layout.addLayout(status_row)

        btn_row = QHBoxLayout()
        btn_row.setSpacing(6)
        self.train_btn = QPushButton("训练 (4组)")
        self.train_btn.setObjectName("trainBtn")
        self.train_btn.clicked.connect(self.start_training)
        self.train_btn.setEnabled(False)
        btn_row.addWidget(self.train_btn)

        self.test_btn = QPushButton("测试")
        self.test_btn.setObjectName("testBtn")
        self.test_btn.clicked.connect(self.start_testing)
        self.test_btn.setEnabled(False)
        btn_row.addWidget(self.test_btn)

        self.stop_btn = QPushButton("停止")
        self.stop_btn.setObjectName("stopBtn")
        self.stop_btn.clicked.connect(lambda: self.stop_all(True))
        self.stop_btn.setEnabled(False)
        btn_row.addWidget(self.stop_btn)

        self.ssvep_freq_combo = QComboBox()
        self.ssvep_freq_combo.setObjectName("ssvepFreqCombo")
        self.ssvep_freq_combo.setFixedWidth(90)
        for f in TARGET_FREQS:
            self.ssvep_freq_combo.addItem(f"{f:.0f} Hz", f)
        self.ssvep_freq_combo.setCurrentIndex(0)
        self.ssvep_freq_combo.currentIndexChanged.connect(self.on_ssvep_freq_changed)
        btn_row.addWidget(self.ssvep_freq_combo)

        self.ssvep_btn = QPushButton("SSVEP 刺激(闪烁)")
        self.ssvep_btn.setObjectName("ssvepBtn")
        self.ssvep_btn.setCheckable(True)
        self.ssvep_btn.clicked.connect(self.toggle_ssvep_stim)
        self.ssvep_btn.setEnabled(False)
        btn_row.addWidget(self.ssvep_btn)

        self.status_btn = QPushButton("状态")
        self.status_btn.setObjectName("statusBtn")
        self.status_btn.clicked.connect(self.send_status)
        self.status_btn.setEnabled(False)
        btn_row.addWidget(self.status_btn)

        self.ipcdiag_btn = QPushButton("IPCDIAG: ON")
        self.ipcdiag_btn.setObjectName("ipcdiagBtn")
        self.ipcdiag_btn.setCheckable(True)
        self.ipcdiag_btn.setChecked(True)
        self.ipcdiag_btn.clicked.connect(self.toggle_ipcdiag)
        self.ipcdiag_btn.setEnabled(False)
        btn_row.addWidget(self.ipcdiag_btn)

        self.posture_btn = QPushButton("姿态数据: ON")
        self.posture_btn.setObjectName("postureBtn")
        self.posture_btn.setCheckable(True)
        self.posture_btn.setChecked(True)
        self.posture_btn.clicked.connect(self.toggle_posture)
        self.posture_btn.setEnabled(False)
        btn_row.addWidget(self.posture_btn)

        self.record_btn = QPushButton("BDF记录")
        self.record_btn.setObjectName("recordBtn")
        self.record_btn.setCheckable(True)
        self.record_btn.clicked.connect(self.toggle_record)
        self.record_btn.setEnabled(False)
        btn_row.addWidget(self.record_btn)

        self.cmd_edit = QLineEdit()
        self.cmd_edit.setPlaceholderText("手动命令(如 MODE,SET,1)...")
        self.cmd_edit.returnPressed.connect(self.send_manual_cmd)
        btn_row.addWidget(self.cmd_edit)

        control_layout.addLayout(btn_row)
        ctrl_layout.addWidget(control_group, stretch=2)
        main_layout.addLayout(ctrl_layout)

        # ---- 3. 中央反馈 + SSVEP 刺激 ----
        center_layout = QHBoxLayout()
        center_layout.setSpacing(10)

        self.arrow_group = QGroupBox("状态反馈")
        arrow_layout = QVBoxLayout(self.arrow_group)
        arrow_layout.setContentsMargins(15, 15, 15, 15)
        self.arrow_label = QLabel()
        self.arrow_label.setObjectName("arrowLabel")
        self.arrow_label.setAlignment(Qt.AlignCenter)
        self.arrow_label.setMinimumHeight(80)
        self.arrow_label.setFont(QFont("Segoe UI", 28, QFont.Bold))
        self.arrow_label.setText("空闲")
        self.arrow_label.setStyleSheet("color: #2f3640;")
        arrow_layout.addWidget(self.arrow_label)
        center_layout.addWidget(self.arrow_group, stretch=1)

        self.stim_group = QGroupBox("SSVEP 刺激")
        stim_layout = QVBoxLayout(self.stim_group)
        stim_layout.setContentsMargins(8, 8, 8, 8)
        self.stim_widget = SsvepStimWidget()
        self.stim_widget.setFocusPolicy(Qt.ClickFocus)
        stim_layout.addWidget(self.stim_widget)

        ssvep_info_row = QHBoxLayout()
        ssvep_info_row.addWidget(QLabel("FBCCA计算:"))
        self.fbcca_source_combo = QComboBox()
        self.fbcca_source_combo.addItem("Python端", "python")
        self.fbcca_source_combo.addItem("MCU端", "mcu")
        self.fbcca_source_combo.currentIndexChanged.connect(self.on_fbcca_source_changed)
        ssvep_info_row.addWidget(self.fbcca_source_combo)
        self.ssvep_info_label = QLabel("FBCCA 结果: 等待数据...")
        self.ssvep_info_label.setStyleSheet("color: #2f3640; font-size: 9pt;")
        ssvep_info_row.addWidget(self.ssvep_info_label, stretch=1)
        stim_layout.addLayout(ssvep_info_row)
        center_layout.addWidget(self.stim_group, stretch=1)
        main_layout.addLayout(center_layout, stretch=1)

        # ---- 4. 进度区 ----
        self.progress_group = QGroupBox("进度")
        progress_layout = QVBoxLayout(self.progress_group)
        progress_layout.setSpacing(6)
        progress_layout.setContentsMargins(15, 12, 15, 12)
        self.progress = QProgressBar()
        self.progress.setRange(0, 100)
        progress_layout.addWidget(self.progress)
        self.stat_label = QLabel("特征: 0 行 | 测试结果: 0")
        self.stat_label.setStyleSheet("color: #2f3640; font-weight: bold; font-size: 9pt;")
        progress_layout.addWidget(self.stat_label)
        self.posture_label = QLabel("姿态: 等待数据")
        self.posture_label.setStyleSheet("color: #00cec9; font-weight: bold; font-size: 9pt;")
        progress_layout.addWidget(self.posture_label)
        main_layout.addWidget(self.progress_group)

        # ---- 5. 日志区 ----
        self.log_tabs = QTabWidget()
        self.log_tabs.setDocumentMode(True)
        self.log_tabs.setTabPosition(QTabWidget.South)

        raw_tab = QWidget()
        raw_layout = QVBoxLayout(raw_tab)
        raw_layout.setContentsMargins(8, 8, 8, 8)
        self.raw_text = QPlainTextEdit()
        self.raw_text.setReadOnly(True)
        self.raw_text.setFont(QFont("Consolas", 9))
        self.raw_text.setMaximumBlockCount(500)
        raw_layout.addWidget(self.raw_text)
        self.log_tabs.addTab(raw_tab, "原始帧日志")

        log_tab = QWidget()
        log_layout = QVBoxLayout(log_tab)
        log_layout.setContentsMargins(8, 8, 8, 8)
        self.result_text = QTextEdit()
        self.result_text.setReadOnly(True)
        self.result_text.setFont(QFont("Consolas", 9))
        log_layout.addWidget(self.result_text)
        self.log_tabs.addTab(log_tab, "解析日志")

        bdf_tab = QWidget()
        bdf_layout = QVBoxLayout(bdf_tab)
        bdf_layout.setContentsMargins(4, 4, 4, 4)
        bdf_ctrl_row = QHBoxLayout()
        bdf_ctrl_row.addWidget(QLabel("BDF文件:"))
        self.bdf_file_edit = QLineEdit()
        self.bdf_file_edit.setReadOnly(True)
        self.bdf_file_edit.setPlaceholderText("点击右侧按钮选择BDF文件...")
        bdf_ctrl_row.addWidget(self.bdf_file_edit, stretch=1)
        bdf_browse_btn = QPushButton("浏览...")
        bdf_browse_btn.clicked.connect(self.bdf_browse_file)
        bdf_ctrl_row.addWidget(bdf_browse_btn)
        bdf_load_btn = QPushButton("加载并显示")
        bdf_load_btn.setObjectName("applyModeBtn")
        bdf_load_btn.clicked.connect(self.bdf_load_and_plot)
        bdf_ctrl_row.addWidget(bdf_load_btn)
        bdf_ctrl_row.addWidget(QLabel("窗口(s):"))
        self.bdf_dur_spin = QSpinBox()
        self.bdf_dur_spin.setRange(1, 60)
        self.bdf_dur_spin.setValue(5)
        self.bdf_dur_spin.valueChanged.connect(lambda: self.bdf_update_plot() if hasattr(self, 'bdf_data') and self.bdf_data else None)
        bdf_ctrl_row.addWidget(self.bdf_dur_spin)
        bdf_ctrl_row.addWidget(QLabel("Y范围:"))
        self.bdf_yrange_combo = QComboBox()
        self.bdf_yrange_combo.addItem("自动", 0)
        for v in [50, 100, 200, 500, 1000]:
            self.bdf_yrange_combo.addItem(f"{v}μV" if v < 1000 else f"{v//1000}mV", v)
        self.bdf_yrange_combo.setCurrentIndex(0)
        self.bdf_yrange_combo.currentIndexChanged.connect(lambda: self.bdf_update_plot() if hasattr(self, 'bdf_data') and self.bdf_data else None)
        bdf_ctrl_row.addWidget(self.bdf_yrange_combo)
        self.bdf_dc_check = QCheckBox("去直流")
        self.bdf_dc_check.setChecked(True)
        self.bdf_dc_check.stateChanged.connect(lambda: self.bdf_update_plot() if hasattr(self, 'bdf_data') and self.bdf_data else None)
        bdf_ctrl_row.addWidget(self.bdf_dc_check)
        bdf_layout.addLayout(bdf_ctrl_row)
        self.bdf_canvas_label = QLabel("请选择BDF文件后点击加载")
        self.bdf_canvas_label.setAlignment(Qt.AlignCenter)
        self.bdf_canvas_label.setStyleSheet("color: #7f8c8d; font-size: 10pt;")
        bdf_layout.addWidget(self.bdf_canvas_label)
        bdf_slider_row = QHBoxLayout()
        self.bdf_pos_label = QLabel("位置: 0.0s / 0.0s")
        self.bdf_pos_label.setMinimumWidth(160)
        bdf_slider_row.addWidget(self.bdf_pos_label)
        self.bdf_slider = QSlider(Qt.Horizontal)
        self.bdf_slider.setRange(0, 0)
        self.bdf_slider.valueChanged.connect(self.bdf_on_slider_changed)
        bdf_slider_row.addWidget(self.bdf_slider, stretch=1)
        bdf_layout.addLayout(bdf_slider_row)
        self.bdf_figure = None
        self.bdf_canvas = None
        self.bdf_data = None
        self.bdf_data_hp = None
        self.log_tabs.addTab(bdf_tab, "波形查看")
        self.log_tabs.currentChanged.connect(self.on_log_tab_changed)
        main_layout.addWidget(self.log_tabs, stretch=5)

        self.update_central_status("空闲", "#7f8c8d")

    # ----------------------- 连接管理 -----------------------
    def on_conn_mode_changed(self, idx):
        self.conn_mode = self.conn_mode_combo.itemData(idx)
        self.conn_stack.setCurrentIndex(idx)
        if self.conn_mode == "tcp":
            self.discover_gateway()

    def refresh_ports(self):
        if self.conn_mode != "serial":
            return
        ports = serial.tools.list_ports.comports()
        current = self.port_combo.currentText()
        self.port_combo.clear()
        for p in ports:
            self.port_combo.addItem(p.device)
        if current and self.port_combo.findText(current) >= 0:
            self.port_combo.setCurrentText(current)

    def discover_gateway(self):
        ip = get_default_gateway()
        self.ip_combo.setCurrentText(ip)
        self.append_log(f"[系统] 自动获取网关IP: {ip}", "green")

    def toggle_connection(self):
        if self.connection and self.connection.isRunning():
            self.connection.stop()
            self.connection = None
            self._on_disconnected()
            return

        if self.conn_mode == "serial":
            port = self.port_combo.currentText()
            if not port:
                QMessageBox.warning(self, "提示", "请选择串口")
                return
            self.connection = SerialConnection(port)
        else:
            ip = self.ip_combo.currentText().strip()
            if not ip:
                ip = get_default_gateway()
                self.ip_combo.setCurrentText(ip)
            try:
                port = int(self.port_edit.text().strip())
            except ValueError:
                QMessageBox.warning(self, "提示", "端口号必须为整数")
                return
            self.connection = TcpConnection(ip, port, port)

        self.connection.frame_received.connect(self.on_frame_received)
        if hasattr(self.connection, 'text_received'):
            self.connection.text_received.connect(self.on_text_received)
        self.connection.status_signal.connect(self.on_conn_status)
        self.connection.start()
        self.connect_btn.setText("断开")
        self.connect_btn.setStyleSheet("background-color: #e84118;")
        self.status_label.setText("连接中...")
        self.status_label.setStyleSheet("color: #e1b12c; font-weight: bold;")
        self.append_log(f"[系统] 正在连接...", "cyan")

    def on_conn_status(self, status):
        if status == "OK":
            self.status_label.setText("已连接")
            self.status_label.setStyleSheet("color: #4cd137; font-weight: bold;")
            self.train_btn.setEnabled(True)
            self.test_btn.setEnabled(True)
            self.apply_mode_btn.setEnabled(True)
            self.ssvep_btn.setEnabled(True)
            self.stop_btn.setEnabled(True)

            self.status_btn.setEnabled(True)
            self.ipcdiag_btn.setEnabled(True)
            self.posture_btn.setEnabled(True)
            self.record_btn.setEnabled(True)
            self.append_log("[系统] 连接成功", "green")
            self.send_command(CMD_STATUS)
            self.update_central_status("空闲", "#7f8c8d")
        elif status.startswith("ERROR:"):
            self.append_log(f"[连接错误] {status[6:]}", "red")
            if self.connection:
                self.connection.stop()
                self.connection = None
            self._on_disconnected()

    def _on_disconnected(self):
        self.connect_btn.setText("连接")
        self.connect_btn.setStyleSheet("background-color: #4cd137;")
        self.status_label.setText("未连接")
        self.status_label.setStyleSheet("color: #e84118; font-weight: bold;")
        self.train_btn.setEnabled(False)
        self.test_btn.setEnabled(False)
        self.stop_btn.setEnabled(False)
        self.apply_mode_btn.setEnabled(False)
        self.ssvep_btn.setEnabled(False)

        self.status_btn.setEnabled(False)
        self.ipcdiag_btn.setEnabled(False)
        self.posture_btn.setEnabled(False)
        self.record_btn.setEnabled(False)
        self.append_log("[系统] 已断开", "gray")
        self.update_central_status("空闲 (未连接)", "#7f8c8d")

    def send_command(self, cmd, data=b''):
        if self.connection:
            name = CMD_NAMES.get(cmd, f"0x{cmd:02X}")
            data_hex = data.hex(' ') if data else ""
            ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
            self.raw_text.appendPlainText(f"[{ts}] ↓发送 {name}({len(data)}B) {data_hex}")
            self.connection.send_frame(cmd, data)

    # ----------------------- 帧接收 -----------------------
    def on_text_received(self, line):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        self.raw_text.appendPlainText(f"[{ts}] {line}")

    def on_frame_received(self, cmd, payload):
        ts = datetime.now().strftime("%H:%M:%S.%f")[:-3]
        name = CMD_NAMES.get(cmd, f"0x{cmd:02X}")
        self.raw_text.appendPlainText(f"[{ts}] ↑接收 {name}({len(payload)}B) {payload[:32].hex(' ')}")

        if cmd == CMD_WAVE:
            self._handle_wave(payload)
        elif cmd == CMD_DIRCSV:
            self._handle_dircsv(payload)
        elif cmd == CMD_RESULT_MI:
            self._handle_result_mi(payload)
        elif cmd == CMD_TASK_START:
            self._handle_task_start(payload)
        elif cmd == CMD_TASK_DONE:
            self.append_log("[↑] TASK_DONE 试次完成", "green")
            self.on_trial_done()
        elif cmd == CMD_TASK_STOPPED:
            self.append_log("[↑] TASK_STOPPED 已停止", "orange")
            self.stop_all(False)
        elif cmd == CMD_READY_TRAIN:
            self.append_log("[↑] READY_TRAIN 训练模式就绪", "green")
            self.mode_label.setText("训练中")
            self.mode_label.setStyleSheet("color: #00a8ff; font-weight: bold;")
            self.update_central_status("训练中", "#00a8ff")
            self.waiting_for_ready = False
            if self.is_training:
                self.send_next_trial()
        elif cmd == CMD_READY_TEST:
            self.append_log("[↑] READY_TEST 测试模式就绪", "green")
            self.mode_label.setText("测试中")
            self.mode_label.setStyleSheet("color: #e1b12c; font-weight: bold;")
            self.update_central_status("测试中", "#e1b12c")
            self.waiting_for_ready = False
        elif cmd == CMD_MODE_SET_OK:
            self._handle_mode_set_ok(payload)
        elif cmd == CMD_RESULT_SSVEP:
            self._handle_result_ssvep(payload)
        elif cmd == CMD_SPECTRUM:
            self._handle_spectrum(payload)
        elif cmd == CMD_EVENT:
            self._handle_event(payload)
        elif cmd == CMD_DIAG:
            self._handle_diag(payload)
        elif cmd == CMD_STATUS:
            self._handle_status(payload)
        else:
            self.append_log(f"[↑] {name} len={len(payload)}", "gray")

    def _handle_status(self, payload):
        if len(payload) < 6:
            return
        work_mode = payload[0]
        trial_state = payload[1]
        row_count = payload[2] | (payload[5] << 8)
        model_select = payload[4]
        work_names = {0: "空闲", 1: "训练中", 2: "测试中"}
        state_names = {0: "无试次", 1: "试次进行中", 2: "试次完成"}
        model_names = {0: "AUTO", 1: "FFT24", 2: "CSP"}
        wn = work_names.get(work_mode, f"未知({work_mode})")
        sn = state_names.get(trial_state, f"未知({trial_state})")
        mn = model_names.get(model_select, f"未知({model_select})")
        ssvep_stim = "闪烁开" if self.ssvep_btn.isChecked() else "闪烁关"
        ssvep_src = "Python" if self.fbcca_source == "python" else "MCU"
        self.append_log(
            f"[↑] STATUS 工作模式={wn} 试次状态={sn} 行数={row_count} 模型={mn} "
            f"SSVEP[{ssvep_stim}/{ssvep_src}]", "cyan"
        )
        if work_mode in (0, 1, 2):
            colors = {0: "#7f8c8d", 1: "#00a8ff", 2: "#e1b12c"}
            self.mode_label.setText(wn)
            self.mode_label.setStyleSheet(f"color: {colors[work_mode]}; font-weight: bold;")

    def _handle_event(self, payload):
        if len(payload) < 2:
            return
        seq = payload[0]
        event_type = payload[1]
        if event_type == 0:  # POSTURE_STATE
            if len(payload) < 7:
                return
            posture_map = {1: "SUPINE", 2: "PRONE", 3: "LEFT", 4: "RIGHT", 5: "SITTING", 0: "UNKNOWN"}
            self.posture_name = posture_map.get(payload[2], "UNKNOWN")
            self.turn_count = struct.unpack_from('<I', payload, 3)[0]
            self.update_posture_display()
        elif event_type == 1:  # TURN
            if len(payload) < 12:
                return
            self.turn_count = struct.unpack_from('<I', payload, 2)[0]
            posture_map = {1: "SUPINE", 2: "PRONE", 3: "LEFT", 4: "RIGHT", 5: "SITTING"}
            turn_to = posture_map.get(payload[7], "UNKNOWN")
            self.posture_name = turn_to
            self.append_log(f"[↑] EVENT 翻身 第{self.turn_count}次 -> {turn_to}", "#00cec9")
            self.update_posture_display()
        elif event_type == 2:  # FALL
            self.fall_detected = True
            self.append_log("[↑] EVENT !!坠床!!", "red")
            self.update_posture_display()
            QMessageBox.critical(self, "坠床警报", "患者发生坠床！需要帮助！\n\n点击确定确认警报。")
            self.send_command(CMD_ALARM_ACK, bytes([seq]))
            self.clear_fall_alert()
        elif event_type == 3:  # NO_TURN
            if len(payload) < 6:
                return
            dur = struct.unpack_from('<I', payload, 2)[0]
            self.append_log(f"[↑] EVENT 未翻身报警 已{dur}分钟未翻身!", "red")

        self.send_command(CMD_ACK, bytes([seq]))

    def _handle_spectrum(self, payload):
        if len(payload) < 5:
            return
        ch = payload[0]
        spec_type = payload[1]
        frag_idx = payload[2]
        total_frags = payload[3]
        data_len = len(payload) - 4
        num_floats = data_len // 4
        if num_floats > 0:
            vals = struct.unpack_from(f'<{num_floats}f', payload, 4)
            val_str = ", ".join(f"{v:.2f}" for v in vals[:4])
            self.append_log(
                f"[↑] SPECTRUM ch={ch} type={spec_type} frag={frag_idx}/{total_frags} "
                f"floats={num_floats} 前4值:[{val_str}]", "gray"
            )

    def _handle_diag(self, payload):
        if len(payload) < 1:
            return
        diag_type = payload[0]
        diag_names = {0: "IPCDIAG", 1: "V5F_DIAG", 2: "SSVEP_DIAG", 3: "MODE_DIAG", 4: "V5F_RAW", 5: "POSTURE", 6: "POSTURE_ANGLE"}
        name = diag_names.get(diag_type, f"UNKNOWN({diag_type})")
        if diag_type == 0 and len(payload) >= 38:
            ack, notify, ok, bad, v5fhb, wfi_wake = struct.unpack_from('<6i', payload, 1)
            v5f_active = payload[25]
            ena, sts, isr = struct.unpack_from('<3I', payload, 26)
            total = ok + bad
            bad_rate = (bad / total * 100.0) if total > 0 else 0.0
            active_names = {0: "IDLE", 1: "COLLECT", 2: "INFER", 3: "CSP"}
            an = active_names.get(v5f_active, str(v5f_active))
            extra = ""
            if len(payload) >= 51:
                send_us, late_us, frame_cnt = struct.unpack_from('<3I', payload, 38)
                ssvep_active = payload[50]
                if ssvep_active:
                    extra = f" ssvep_send={send_us}us late={late_us}us frm={frame_cnt}"
                    if len(payload) >= 59:
                        rp_us, rp_count = struct.unpack_from('<2I', payload, 51)
                        extra += f" rp={rp_us}us rpcnt={rp_count}"
            self.append_log(
                f"[↑] DIAG IPCDIAG: ack={ack} notify={notify} parse_ok={ok} parse_bad={bad} "
                f"bad率={bad_rate:.3f}% v5f_handler={v5fhb} wfi_wake={wfi_wake} "
                f"v5f_active={an} ENA={ena} STS={sts} ISR={isr}{extra}", "gray"
            )
        else:
            self.append_log(f"[↑] DIAG {name} len={len(payload)}", "gray")

    def _handle_wave(self, payload):
        if len(payload) < WAVE_PAYLOAD:
            return
        wave_type = payload[0]
        vals = struct.unpack_from(f'<{WAVE_NUM_CH}f', payload, 1)
        if wave_type == 0 and self.fbcca_source == "python":
            mv = vals[SSVEP_CHANNEL] * 1000.0
            self._process_ssvep_sample(mv)

    def _handle_dircsv(self, payload):
        if len(payload) < 1 + 24 * 4:
            return
        label = payload[0]
        features = list(struct.unpack_from('<24i', payload, 1))
        self.train_data.append((label, features))
        self.received_feature_count += 1
        self.stat_label.setText(f"特征: {self.received_feature_count} 行 | 测试结果: {len(self.test_results)}")
        if self.is_training:
            total = len(self.trial_sequence)
            done = len(self.train_data) // 20
            self.progress.setValue(min(100, int(done / total * 100)))

    def _handle_result_mi(self, payload):
        if len(payload) < 16:
            return
        seq = payload[0]
        pred = payload[1]
        score_l, score_r, conf = struct.unpack_from('<3i', payload, 2)
        trained = payload[14] == 1
        intent = "LEFT" if pred == 0 else "RIGHT"
        self.test_results.append((intent, score_l, score_r))
        self.stat_label.setText(f"特征: {self.received_feature_count} 行 | 测试结果: {len(self.test_results)}")
        if intent == "LEFT":
            self.show_arrow_left()
            color = "blue"
        else:
            self.show_arrow_right()
            color = "red"
        self.append_log(f"[↑] RESULT_MI {intent} (左:{score_l}, 右:{score_r}, 置信度:{conf}, trained:{trained})", color)
        self.send_command(CMD_ACK, bytes([seq]))

    def _handle_task_start(self, payload):
        if len(payload) < 1:
            return
        side = payload[0]
        if side == 0:
            self.show_arrow_left()
            self.append_log("[↑] TASK_START 左手任务开始", "blue")
        else:
            self.append_log("[↑] TASK_START 右手任务开始", "red")
        if self.is_training:
            total = len(self.trial_sequence)
            cur = self.current_trial_index + 1
            self.trial_info.setText(f"试次 {cur}/{total} ({'左' if side==0 else '右'})")

    def _handle_mode_set_ok(self, payload):
        if len(payload) < 2:
            return
        mode = payload[1]
        mode_names = {1: "COLLECT", 2: "INFER"}
        name = mode_names.get(mode, "UNKNOWN")
        self.mode_status_label.setText(name)
        color = "blue" if mode == 1 else "red" if mode == 2 else "green"
        self.mode_status_label.setStyleSheet(f"color: {color}; font-weight: bold;")
        self.append_log(f"[↑] MODE_SET_OK 模式已切换至: {name}", "cyan")
        idx = self.mode_combo.findData(mode)
        if idx >= 0:
            self.mode_combo.setCurrentIndex(idx)

        if self.pending_train:
            self.pending_train = False
            self.send_command(CMD_MODE_TRAIN)
            self.append_log("[↓] 发送 MODE_TRAIN", "magenta")
        if self.pending_test:
            self.pending_test = False
            self.send_command(CMD_MODE_TEST)
            self.append_log("[↓] 发送 MODE_TEST", "magenta")

    def _handle_result_ssvep(self, payload):
        if len(payload) < 38:
            return
        seq = struct.unpack_from('<I', payload, 0)[0]
        raw_index = payload[4]
        voted_index = payload[5]
        if raw_index == 0xFF:
            raw_index = -1
        if voted_index == 0xFF:
            voted_index = -1
        ratio, best, margin = struct.unpack_from('<3i', payload, 6)
        scores = list(struct.unpack_from('<4i', payload, 18))
        votes = list(payload[34:38])
        raw_str = f"{TARGET_FREQS[raw_index]:.1f}Hz" if 0 <= raw_index < len(TARGET_FREQS) else "无"
        voted_str = f"{TARGET_FREQS[voted_index]:.1f}Hz" if 0 <= voted_index < len(TARGET_FREQS) else "无"
        self.append_log(
            f"[↑] RESULT_SSVEP raw={raw_str} voted={voted_str} ratio={ratio/10000:.2f} best={best/10000:.4f}",
            "green"
        )
        if self.fbcca_source == "mcu":
            score_str = " | ".join(f"{TARGET_FREQS[i]:.1f}:{scores[i]/10000:.4f}" for i in range(min(4, len(TARGET_FREQS))))
            self.ssvep_info_label.setText(
                f"FBCCA(MCU): raw={raw_str} voted={voted_str} | ratio={ratio/10000:.2f} | {score_str}"
            )
        self.send_command(CMD_ACK, bytes([seq & 0xFF]))

    # ----------------------- SSVEP 分析 -----------------------
    def _process_ssvep_sample(self, mv_raw):
        self.ssvep_sample_count += 1
        mv_analysis = self.ssvep_analysis_filter.process(mv_raw)
        self.ssvep_buffer.append(mv_analysis)
        self.ssvep_since_last_update += 1

        enough_window = len(self.ssvep_buffer) >= FBCCA_WINDOW_SIZE
        enough_step = self.ssvep_since_last_update >= FBCCA_STEP_SIZE

        if enough_window and enough_step:
            self.ssvep_since_last_update = 0
            data_ch = np.array(list(self.ssvep_buffer)[-FBCCA_WINDOW_SIZE:])
            if ENABLE_CHANNEL_QUALITY_CHECK and not is_channel_usable(data_ch):
                self.ssvep_result = "UNCERTAIN"
                self._update_ssvep_info_label()
                return

            eeg_data = np.array([data_ch])
            raw_predicted, raw_scores = fbcca_predict(
                eeg_data, TARGET_FREQS, SAMPLE_RATE, FBCCA_HARMONICS, FILTER_BANKS
            )
            chosen_freq, ratio, best_score, margin = choose_fbcca_result(
                raw_scores, FBCCA_RATIO_THRESHOLD, FBCCA_MIN_SCORE, FBCCA_MARGIN_THRESHOLD
            )
            voted_freq, vote_counter = vote_result(chosen_freq)

            self.ssvep_scores = raw_scores
            self.ssvep_ratio = ratio
            self.ssvep_best_score = best_score
            self.ssvep_margin = margin
            self.ssvep_vote_counter = vote_counter
            self.ssvep_result = voted_freq if voted_freq is not None else "UNCERTAIN"
            self.ssvep_round_index += 1
            self._update_ssvep_info_label()

    def _update_ssvep_info_label(self):
        if self.ssvep_result is None or self.ssvep_result == "UNCERTAIN":
            res_str = "不确定"
        else:
            res_str = f"{self.ssvep_result:.2f} Hz"
        score_str = " | ".join(f"{f:.1f}:{self.ssvep_scores.get(f,0):.4f}" for f in TARGET_FREQS)
        self.ssvep_info_label.setText(
            f"FBCCA: {res_str} | ratio={self.ssvep_ratio:.2f} | {score_str} | 采样:{self.ssvep_sample_count}"
        )

    # ----------------------- SSVEP 刺激控制 -----------------------
    def on_fbcca_source_changed(self, idx):
        self.fbcca_source = self.fbcca_source_combo.itemData(idx)
        if self.fbcca_source == "python":
            self.append_log("[↓] FBCCA→Python端，发送 SSVEP_STOP", "cyan")
            self.ssvep_info_label.setText("FBCCA(Python): 等待数据...")
            self.send_command(CMD_SSVEP_STOP)
        else:
            freq_idx = self.ssvep_freq_combo.currentIndex()
            self.append_log(f"[↓] FBCCA→MCU端，发送 SSVEP_START idx={freq_idx}", "cyan")
            self.ssvep_info_label.setText("FBCCA(MCU): 等待数据...")
            self.send_command(CMD_SSVEP_START, bytes([freq_idx]))

    def on_ssvep_freq_changed(self, idx):
        """切换频率后自动同步stim widget并下发命令"""
        freq = self.ssvep_freq_combo.currentData()
        if hasattr(self, 'stim_widget') and self.stim_widget is not None:
            self.stim_widget.set_freq(float(freq))
            self.stim_widget.freq_index = idx
        self.append_log(f"[↓] SSVEP 频率切换 → {freq:.0f}Hz idx={idx}", "magenta")
        if self.connection:
            self.send_command(CMD_SSVEP_START, bytes([idx]))

    def toggle_ssvep_stim(self):
        if self.ssvep_btn.isChecked():
            freq = self.ssvep_freq_combo.currentData()
            self.stim_widget.set_freq(float(freq))
            freq_idx = self.ssvep_freq_combo.currentIndex()
            self.stim_widget.freq_index = freq_idx
            self.stim_widget.start_stim()
            self.send_command(CMD_SSVEP_START, bytes([freq_idx]))
            self.append_log(f"[↓] SSVEP 刺激启动 频率={freq:.0f}Hz idx={freq_idx}", "magenta")
            self.ssvep_btn.setText("停止 SSVEP 刺激")
            self.stop_btn.setEnabled(True)
            self.mode_label.setText("SSVEP刺激中")
            self.mode_label.setStyleSheet("color: #9c88ff; font-weight: bold;")
            self.update_central_status("SSVEP刺激中", "#9c88ff")
        else:
            self.stim_widget.stop_stim()
            self.send_command(CMD_SSVEP_STOP)
            self.ssvep_btn.setText("SSVEP 刺激(闪烁)")
            if not self.is_training and not self.is_testing:

                self.mode_label.setText("空闲")
                self.mode_label.setStyleSheet("color: #7f8c8d; font-weight: bold;")
                self.update_central_status("空闲", "#7f8c8d")

    def toggle_record(self):
        if self.record_btn.isChecked():
            self.send_command(CMD_RECORD_START)
            self.append_log("[↓] BDF记录启动", "magenta")
            self.record_btn.setText("停止BDF记录")
        else:
            self.send_command(CMD_RECORD_STOP)
            self.append_log("[↓] BDF记录停止", "magenta")
            self.record_btn.setText("BDF记录")

    # ----------------------- BDF 波形查看 -----------------------
    def on_log_tab_changed(self, idx):
        is_bdf = (idx == 2)
        self.arrow_group.setVisible(not is_bdf)
        self.stim_group.setVisible(not is_bdf)
        self.progress_group.setVisible(not is_bdf)

    def bdf_browse_file(self):
        path, _ = QFileDialog.getOpenFileName(
            self, "选择BDF文件", "", "BDF文件 (*.bdf);;所有文件 (*)"
        )
        if path:
            self.bdf_file_edit.setText(path)

    def bdf_load_and_plot(self):
        path = self.bdf_file_edit.text().strip()
        if not path:
            QMessageBox.warning(self, "提示", "请先选择BDF文件")
            return
        try:
            data = self._parse_bdf(path)
        except Exception as e:
            QMessageBox.critical(self, "BDF解析错误", str(e))
            return
        if data is None:
            QMessageBox.warning(self, "提示", "无法解析BDF文件")
            return
        self.bdf_data = data
        channels, fs, n_samples, labels = data
        b_hp, a_hp = butter(2, 1.0 / (fs / 2.0), btype='high')
        channels_hp = [filtfilt(b_hp, a_hp, channels[ch].astype(float)) for ch in range(len(channels))]
        self.bdf_data_hp = (channels_hp, fs, n_samples, labels)
        if self.bdf_canvas is not None:
            self.bdf_canvas.setParent(None)
            self.bdf_canvas = None
            self.bdf_figure = None
        self.bdf_figure = Figure(figsize=(12, 8), facecolor='#101820')
        self.bdf_ax = self.bdf_figure.add_subplot(111, facecolor='#101820')
        canvas = FigureCanvas(self.bdf_figure)
        canvas.setParent(self)
        self.bdf_canvas = canvas
        bdf_layout = self.bdf_canvas_label.parent().layout()
        bdf_layout.replaceWidget(self.bdf_canvas_label, canvas)
        self.bdf_canvas_label.hide()
        window_samples = self.bdf_dur_spin.value() * fs
        max_pos = max(0, n_samples - window_samples)
        self.bdf_slider.setRange(0, max_pos)
        self.bdf_slider.setValue(0)
        self.bdf_update_plot()

    def bdf_on_slider_changed(self, val):
        if self.bdf_data:
            self.bdf_update_plot()

    def bdf_update_plot(self):
        if not self.bdf_data or not self.bdf_figure:
            return
        channels, fs, n_samples, labels = self.bdf_data
        n_ch = len(channels)
        ax = self.bdf_ax
        ax.clear()
        ax.set_facecolor('#101820')
        y_range = self.bdf_yrange_combo.currentData()
        do_dc = self.bdf_dc_check.isChecked()
        if do_dc and self.bdf_data_hp:
            channels = self.bdf_data_hp[0]
        window_samples = self.bdf_dur_spin.value() * fs
        start_idx = self.bdf_slider.value()
        end_idx = min(start_idx + window_samples, n_samples)
        if end_idx <= start_idx:
            return
        self.bdf_pos_label.setText(f"位置: {start_idx/fs:.2f}s / {n_samples/fs:.2f}s")
        t = np.arange(start_idx, end_idx) / fs
        segs = []
        for ch in range(n_ch):
            seg = channels[ch][start_idx:end_idx].astype(float)
            segs.append(seg)
        if y_range == 0:
            all_data = np.concatenate(segs) if segs else np.array([0])
            y_range = max(np.percentile(np.abs(all_data), 99), 10)
        ch_spacing = 2 * y_range * 1.3
        label_colors = {
            'OZ': '#4FC3F7', 'O1': '#FF8A65', 'F3': '#81C784', 'F4': '#FFD54F',
            'CP3': '#7C4DFF', 'CP4': '#4DD0E1', 'C3': '#F06292', 'C4': '#A1887F',
        }
        for ch in range(n_ch):
            color = label_colors.get(labels[ch].upper().strip(), '#FFFFFF')
            seg = segs[ch]
            offset = (n_ch - 1 - ch) * ch_spacing
            ax.plot(t, seg + offset, linewidth=3, color=color, alpha=0.12, solid_capstyle='round')
            ax.plot(t, seg + offset, linewidth=0.8, color=color, solid_capstyle='round')
            ax.text(t[-1] + (t[-1] - t[0]) * 0.012, offset, labels[ch],
                    color=color, fontsize=8, va='center', ha='left', fontweight='bold')
            ax.axhline(y=offset, color='#2A3441', linewidth=0.3, alpha=0.5)
        for ch in range(n_ch + 1):
            y = ch * ch_spacing
            ax.axhline(y=y, color='#3A4A5C', linewidth=0.5, linestyle='--', alpha=0.5)
        ax.set_xlim(t[0], t[-1])
        ax.set_ylim(-ch_spacing * 0.5, n_ch * ch_spacing + ch_spacing * 0.5)
        ax.tick_params(colors='#90A4AE', labelsize=7)
        for spine in ax.spines.values():
            spine.set_color('#2A3441')
        ax.set_xlabel("时间 (s)", color='#90A4AE', fontsize=8)
        ax.set_yticks([])
        ax.grid(False)
        self.bdf_figure.tight_layout()
        self.bdf_canvas.draw()

    @staticmethod
    def _parse_bdf(path):
        with open(path, 'rb') as f:
            raw = f.read()
        if len(raw) < 256:
            return None

        def _s(b):
            return b.decode('ascii', errors='replace').replace('\x00', '').strip()

        version = _s(raw[0:8])
        n_records = int(_s(raw[236:244]))
        dur_raw = _s(raw[244:252])
        duration = float(dur_raw) if dur_raw else 1.0
        n_ch = int(_s(raw[252:256]))
        if n_ch <= 0 or n_ch > 64:
            return None
        labels = []
        for i in range(n_ch):
            lbl = _s(raw[256 + i * 16:256 + i * 16 + 16])
            labels.append(lbl if lbl else f"Ch{i}")
        srate_raw = _s(raw[1984:1992])
        fs = int(srate_raw) if srate_raw else 250
        phys_min = []
        phys_max = []
        dig_min = []
        dig_max = []
        for i in range(n_ch):
            pmin_s = _s(raw[1088 + i * 8:1096 + i * 8])
            pmax_s = _s(raw[1152 + i * 8:1160 + i * 8])
            dmin_s = _s(raw[1216 + i * 8:1224 + i * 8])
            dmax_s = _s(raw[1280 + i * 8:1288 + i * 8])
            pmin = float(pmin_s) if pmin_s else -187500.0
            pmax = float(pmax_s) if pmax_s else 187500.0
            dmin = float(dmin_s) if dmin_s else -8388608.0
            dmax = float(dmax_s) if dmax_s else 8388607.0
            phys_min.append(pmin)
            phys_max.append(pmax)
            dig_min.append(dmin)
            dig_max.append(dmax)
        header_size = 256 + n_ch * 256
        data_bytes = len(raw) - header_size
        sample_per_record = fs * int(duration) if duration >= 1 else fs
        bytes_per_sample = 3
        record_size = n_ch * sample_per_record * bytes_per_sample
        if record_size == 0:
            return None
        n_avail = data_bytes // record_size
        if n_avail < n_records:
            n_records = n_avail
        data = np.frombuffer(raw[header_size:header_size + n_records * record_size], dtype=np.uint8)
        data = data.reshape(n_records, n_ch, sample_per_record, bytes_per_sample)
        b1 = data[:, :, :, 0].astype(np.int32)
        b2 = data[:, :, :, 1].astype(np.int32)
        b3 = data[:, :, :, 2].astype(np.int32)
        val = b1 | (b2 << 8) | (b3 << 16)
        val = np.where(val >= 0x800000, val - 0x1000000, val)
        val = val.reshape(n_ch, n_records * sample_per_record)
        channels = []
        for i in range(n_ch):
            dmin = dig_min[i]
            dmax = dig_max[i]
            pmin = phys_min[i]
            pmax = phys_max[i]
            if dmax == dmin:
                ch = val[i].astype(float)
            else:
                ch = (val[i].astype(float) - dmin) / (dmax - dmin) * (pmax - pmin) + pmin
            channels.append(ch)
        n_total = n_records * sample_per_record
        return channels, fs, n_total, labels

    # ----------------------- 中央状态 -----------------------
    def update_central_status(self, text, color="#2f3640", is_arrow=False):
        if is_arrow:
            border_color = "#0984e3" if "左" in text else "#e84118"
            self.arrow_label.setStyleSheet(
                f"color: {color}; background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                f"stop:0 #dfe6e9, stop:1 #b2bec3); font-size: 32pt; font-weight: bold; "
                f"border-radius: 12px; border: 3px solid {border_color};"
            )
        else:
            self.arrow_label.setStyleSheet(
                f"color: {color}; background: qlineargradient(x1:0, y1:0, x2:0, y2:1,"
                f"stop:0 #f0f2f5, stop:1 #dcdde1); font-size: 22pt; font-weight: bold; "
                f"border-radius: 12px; border: 2px solid #d0d7de;"
            )
        self.arrow_label.setText(text)

    def show_arrow_left(self):
        self.update_central_status("← 左手", "#0984e3", is_arrow=True)

    def show_arrow_right(self):
        self.update_central_status("→ 右手", "#e84118", is_arrow=True)

    # ----------------------- 日志 -----------------------
    def append_log(self, text, color=None):
        if color:
            self.result_text.append(f'<span style="color:{color};">{text}</span>')
        else:
            self.result_text.append(text)
        self.result_text.moveCursor(QTextCursor.End)

    # ----------------------- 模式/训练/测试 -----------------------
    def apply_mode(self):
        if not self.connection:
            QMessageBox.warning(self, "提示", "请先连接")
            return
        mode = self.mode_combo.currentData()
        self.send_command(CMD_MODE_SET, bytes([mode]))
        self.append_log(f"[↓] 发送模式切换: {mode}", "cyan")

    def send_status(self):
        self.send_command(CMD_STATUS)

    def toggle_ipcdiag(self):
        if not self.connection:
            return
        if self.ipcdiag_btn.isChecked():
            self.send_command(CMD_IPCDIAG, bytes([1]))
            self.ipcdiag_btn.setText("IPCDIAG: ON")
            self.append_log("[↓] IPCDIAG 已开启", "green")
        else:
            self.send_command(CMD_IPCDIAG, bytes([0]))
            self.ipcdiag_btn.setText("IPCDIAG: OFF")
            self.append_log("[↓] IPCDIAG 已关闭", "orange")

    def toggle_posture(self):
        if not self.connection:
            return
        if self.posture_btn.isChecked():
            self.send_command(CMD_POSTURE, bytes([1]))
            self.posture_btn.setText("姿态数据: ON")
            self.append_log("[↓] 姿态数据流已开启", "green")
        else:
            self.send_command(CMD_POSTURE, bytes([0]))
            self.posture_btn.setText("姿态数据: OFF")
            self.append_log("[↓] 姿态数据流已关闭", "orange")

    def send_manual_cmd(self):
        cmd = self.cmd_edit.text().strip()
        if not cmd or not self.connection:
            return
        text_map = {
            "MODE,SET,1": (CMD_MODE_SET, bytes([1])),
            "MODE,SET,2": (CMD_MODE_SET, bytes([2])),
            "MODE,SET,3": (CMD_MODE_SET, bytes([3])),
            "MODE,TRAIN": (CMD_MODE_TRAIN, b""),
            "MODE,TEST": (CMD_MODE_TEST, b""),
            "TRIAL,LEFT": (CMD_TRIAL, bytes([0])),
            "TRIAL,RIGHT": (CMD_TRIAL, bytes([1])),
            "STOP": (CMD_STOP, b""),
            "STATUS": (CMD_STATUS, b""),
            "SSVEP,START": (CMD_SSVEP_START, b""),
            "SSVEP,START,0": (CMD_SSVEP_START, bytes([0])),
            "SSVEP,START,1": (CMD_SSVEP_START, bytes([1])),
            "SSVEP,START,2": (CMD_SSVEP_START, bytes([2])),
            "SSVEP,START,3": (CMD_SSVEP_START, bytes([3])),
            "SSVEP,STOP": (CMD_SSVEP_STOP, b""),
            "DISPLAY,0": (CMD_DISPLAY_CFG, bytes([0])),
            "DISPLAY,1": (CMD_DISPLAY_CFG, bytes([1])),
            "DISPLAY,2": (CMD_DISPLAY_CFG, bytes([2])),
        }

        match = text_map.get(cmd.upper())
        if match:
            if cmd.upper() == "SSVEP,START" and len(match[1]) == 0:
                idx = self.ssvep_freq_combo.currentIndex()
                match = (CMD_SSVEP_START, bytes([idx]))
            self.send_command(match[0], match[1])
            self.append_log(f"[↓] 手动发送 {cmd} -> 0x{match[0]:02X}", "magenta")
        else:
            self.append_log(f"[↓] 手动发送 未知命令: {cmd}", "red")
        self.cmd_edit.clear()

    def update_posture_display(self):
        posture_cn = {
            "SUPINE": "仰躺", "PRONE": "趴着",
            "LEFT": "左侧卧", "RIGHT": "右侧卧",
            "SITTING": "坐起", "UNKNOWN": "未知"
        }
        name = posture_cn.get(self.posture_name, self.posture_name)
        conf_pct = self.posture_conf / 100.0
        stable_s = self.posture_stable_ms / 1000.0
        if self.fall_detected:
            self.posture_label.setText(
                f"!! 坠床报警 !!  姿态: {name}  翻身: {self.turn_count}次"
            )
            self.posture_label.setStyleSheet("color: #e84118; font-weight: bold; font-size: 11pt; background-color: #ffeaa7;")
            return
        self.posture_label.setText(
            f"姿态: {name} ({conf_pct:.0f}%)  稳定: {stable_s:.1f}s  "
            f"重力: X={self.posture_gx} Y={self.posture_gy} Z={self.posture_gz}  "
            f"翻身: {self.turn_count}次"
        )
        self.posture_label.setStyleSheet("color: #00cec9; font-weight: bold; font-size: 9pt;")

    def clear_fall_alert(self):
        self.fall_detected = False
        self.update_posture_display()

    def start_training(self):
        if not self.connection:
            return
        self.mode_combo.setCurrentIndex(0)
        mode = 1

        self.trial_sequence = ['LEFT', 'RIGHT', 'LEFT', 'RIGHT']
        self.current_trial_index = 0
        self.train_data.clear()
        self.received_feature_count = 0
        self.is_training = True
        self.is_testing = False
        self.waiting_for_ready = True
        self.pending_train = True
        self.pending_test = False
        self.progress.setValue(0)
        self.stat_label.setText("特征: 0 行 | 测试结果: 0")
        self.trial_info.setText("")

        self.append_log("=" * 60, "blue")
        self.append_log("=== 开始训练（4组，左右左右） ===", "blue")
        self.append_log("=" * 60, "blue")

        self.train_btn.setEnabled(False)
        self.test_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.update_central_status("训练启动中...", "#00a8ff")
        self.send_command(CMD_MODE_SET, bytes([mode]))

    def send_next_trial(self):
        if self.current_trial_index < len(self.trial_sequence):
            side = self.trial_sequence[self.current_trial_index]
            side_byte = 0 if side == 'LEFT' else 1
            self.send_command(CMD_TRIAL, bytes([side_byte]))
            self.append_log(
                f"[↓] 发送 TRIAL {side} ({self.current_trial_index + 1}/{len(self.trial_sequence)})",
                "magenta"
            )
        else:
            self.append_log("所有试次已发送，等待数据...", "green")

    def on_trial_done(self):
        if self.is_training:
            self.current_trial_index += 1
            if self.current_trial_index < len(self.trial_sequence):
                self.send_next_trial()
            else:
                self.append_log("=" * 60, "green")
                self.append_log(f"=== 训练完成！共接收 {self.received_feature_count} 个特征 ===", "green")
                self.append_log("=" * 60, "green")
                self.stop_all(False)
                self.save_training_data()
                self.update_central_status("训练完成", "#4cd137")

    def save_training_data(self):
        if not self.train_data:
            self.append_log("[警告] 无数据可保存", "orange")
            return
        filename = f"train_DIRCSV_{datetime.now().strftime('%Y%m%d_%H%M%S')}.csv"
        target_dir = Path(__file__).resolve().parent / "direction_24d_pycharm" / "data"
        try:
            target_dir.mkdir(parents=True, exist_ok=True)
            output_path = target_dir / filename
            with open(output_path, 'w', newline='') as f:
                writer = csv.writer(f)
                writer.writerow(['label'] + [f'feat{i}' for i in range(24)])
                for label, feats in self.train_data:
                    writer.writerow([label] + feats)
            self.append_log(f"[保存] DIRCSV 数据已保存至 {output_path.resolve()}", "green")
        except Exception as e:
            self.append_log(f"[错误] 保存失败: {e}", "red")

    def start_testing(self):
        if not self.connection:
            return
        self.mode_combo.setCurrentIndex(1)
        self.pending_test = True
        self.pending_train = False
        self.is_testing = True
        self.is_training = False
        self.test_results.clear()
        self.stat_label.setText(f"特征: {self.received_feature_count} 行 | 测试结果: 0")
        self.append_log("=" * 60, "red")
        self.append_log("=== 开始测试（推理模式） ===", "red")
        self.append_log("=" * 60, "red")
        self.train_btn.setEnabled(False)
        self.test_btn.setEnabled(False)
        self.stop_btn.setEnabled(True)
        self.update_central_status("测试启动中...", "#e1b12c")
        self.send_command(CMD_MODE_SET, bytes([2]))

    def stop_all(self, send_cmd=True):
        if send_cmd and self.connection:
            self.send_command(CMD_SSVEP_STOP)
            self.send_command(CMD_STOP)
        if self.ssvep_btn.isChecked():
            self.ssvep_btn.setChecked(False)
            self.stim_widget.stop_stim()
            self.ssvep_btn.setText("SSVEP 刺激(闪烁)")
        self.is_training = False
        self.is_testing = False
        self.waiting_for_ready = False
        self.pending_train = False
        self.pending_test = False
        self.train_btn.setEnabled(True)
        self.test_btn.setEnabled(True)

        self.progress.setValue(0)
        self.mode_label.setText("空闲")
        self.mode_label.setStyleSheet("color: #7f8c8d; font-weight: bold;")
        self.trial_info.setText("")
        self.append_log("[系统] 已停止所有活动", "orange")
        self.update_central_status("空闲", "#7f8c8d")

    def closeEvent(self, event):
        if self.connection:
            self.connection.stop()
        event.accept()


if __name__ == '__main__':
    app = QApplication(sys.argv)
    win = MainWindow()
    win.show()
    sys.exit(app.exec_())