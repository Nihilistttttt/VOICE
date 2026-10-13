# -*- coding: utf-8 -*-
"""
GLXSS AR眼镜 SD卡固件烧录 + AA55控制工具
直接修改下方参数后运行: python glxss_sd_control.py
控制模式使用AA55二进制协议，与W25Q64项目完全兼容
"""

import os
import struct
import time
import threading

try:
    import serial
except ImportError:
    print("请安装 pyserial: pip install pyserial")
    exit(1)

# ==================== 参数设置 ====================
ACTION      = "burn"        # burn / train / test / ssvep / ssvep_stop / stop / display / trial / mode_set / monitor
SERIAL_PORT = "COM7"
SERIAL_BAUD = 2000000
FW_PATH     = r"D:\Libraries\Projects\EEG\眼镜驱动\G25_MainFW_1.9.2.decrypted.img"
SSVEP_FREQ  = 0             # 0-3 频率索引 (11/13/15/17 Hz)
DISPLAY_MODE= 0             # 0=SSVEP, 1=ARROW, 2=ARROW_TRAIN
TRIAL_SIDE  = 0             # 0=LEFT, 1=RIGHT
COLLECT_MODE= 1             # 1=collect, 2=infer, 3=csp
# ================================================

SECTOR_SIZE = 512

# ==================== AA55 协议 ====================
FRAME_HEADER0 = 0xAA
FRAME_HEADER1 = 0x55
FRAME_TAIL    = 0x7E
ESCAPE_CHAR   = 0x7D
ESCAPE_XOR    = 0x20

ADDR_MCU     = 0x01
ADDR_DOCTOR  = 0x02
ADDR_PATIENT = 0x03
ADDR_PYTHON  = 0x04

CMD_MODE_SET      = 0x01
CMD_MODE_TRAIN    = 0x02
CMD_MODE_TEST     = 0x03
CMD_TRIAL         = 0x06
CMD_STOP          = 0x07
CMD_DISPLAY_CFG   = 0x0B
CMD_SSVEP_START   = 0x0C
CMD_SSVEP_STOP    = 0x0D


def checksum16(data):
    return sum(data) & 0xFFFF


def pack_frame(addr, cmd, payload=b''):
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


def build_downstream_payload(sub_cmd, data=b''):
    return bytes([sub_cmd]) + data


def send_aa55_cmd(ser, cmd, data=b''):
    payload = build_downstream_payload(cmd, data)
    frame = pack_frame(ADDR_PYTHON, cmd, payload)
    ser.write(frame)


# ==================== 日志读取 ====================

def _log_reader(ser, stop_event):
    while not stop_event.is_set():
        b = ser.read(1)
        if b:
            try:
                print(b.decode("utf-8", errors="replace"), end="", flush=True)
            except Exception:
                pass


# ==================== 烧录 ====================

def wait_byte(ser, expected, label):
    text_buf = bytearray()
    while True:
        b = ser.read(1)
        if not b:
            continue
        v = b[0]
        if v == expected:
            if text_buf:
                try:
                    print(text_buf.decode("utf-8", errors="replace"), end="", flush=True)
                except Exception:
                    pass
            return True
        if 0xE0 <= v <= 0xEF:
            if text_buf:
                try:
                    print(text_buf.decode("utf-8", errors="replace"), end="", flush=True)
                except Exception:
                    pass
            print(f"\n[{label}] MCU错误: 0x{v:02X}")
            return False
        text_buf.append(v)
        if len(text_buf) >= 256:
            try:
                print(text_buf.decode("utf-8", errors="replace"), end="", flush=True)
            except Exception:
                pass
            text_buf = bytearray()


def burn_firmware(ser, fw_path):
    if not os.path.exists(fw_path):
        print(f"固件文件不存在: {fw_path}")
        return False

    with open(fw_path, "rb") as f:
        fw_data = f.read()

    fw_size = len(fw_data)
    total_sectors = (fw_size + SECTOR_SIZE - 1) // SECTOR_SIZE
    print(f"[烧录] 固件: {os.path.basename(fw_path)}")
    print(f"[烧录] 大小: {fw_size} 字节 ({fw_size/1024:.1f} KB), {total_sectors} 扇区")

    print("[烧录] 等待MCU就绪(0xAA)...")
    if not wait_byte(ser, 0xAA, "READY"):
        print("[烧录] MCU未就绪")
        return False
    print("[烧录] MCU已就绪，发送固件大小...")

    ser.write(struct.pack('<I', fw_size))

    if not wait_byte(ser, 0xBB, "SIZE_ACK"):
        print("[烧录] 未收到 SIZE_ACK")
        return False

    print("[烧录] 等待SD卡就绪(0xCC)...")
    if not wait_byte(ser, 0xCC, "SD_READY"):
        print("[烧录] SD卡初始化失败")
        return False

    print(f"[烧录] 开始写入 {total_sectors} 扇区...")
    t_start = time.time()

    for sec in range(total_sectors):
        offset = sec * SECTOR_SIZE
        chunk = fw_data[offset:offset + SECTOR_SIZE]
        if len(chunk) < SECTOR_SIZE:
            chunk = chunk + b'\xFF' * (SECTOR_SIZE - len(chunk))

        ser.write(chunk)

        if not wait_byte(ser, 0xEE, f"SEC_{sec}"):
            print(f"\n[烧录] 扇区 {sec} 写入失败")
            return False

        time.sleep(0.002)

        if (sec + 1) % 100 == 0 or sec == total_sectors - 1:
            pct = (sec + 1) * 100 // total_sectors
            elapsed = time.time() - t_start
            print(f"\r  [进度] {pct}% ({sec+1}/{total_sectors} 扇区) {elapsed:.1f}s", end="", flush=True)

    print()
    if not wait_byte(ser, 0xFF, "ALL_DONE"):
        print("[烧录] 未收到 ALL_DONE")
        return False

    elapsed = time.time() - t_start
    print(f"[烧录] 完成! {fw_size/1024:.1f} KB, 耗时 {elapsed:.1f}s")
    return True


# ==================== 串口打开 ====================

def open_serial(timeout=None):
    try:
        ser = serial.Serial()
        ser.port = SERIAL_PORT
        ser.baudrate = SERIAL_BAUD
        ser.dtr = False
        ser.rts = False
        ser.timeout = timeout if timeout is not None else 1.0
        ser.open()
    except Exception as e:
        print(f"无法打开 {SERIAL_PORT}: {e}")
        return None
    print(f"[工具] {SERIAL_PORT} @ {SERIAL_BAUD} baud 已连接")
    ser.reset_input_buffer()
    ser.reset_output_buffer()
    return ser


# ==================== 主逻辑 ====================

def main():
    if ACTION == "burn":
        ser = open_serial(timeout=None)
        if not ser:
            return
        burn_firmware(ser, FW_PATH)
        ser.close()
        return

    ser = open_serial()
    if not ser:
        return

    if ACTION == "train":
        print("[控制] 启动训练模式 (COLLECT mode=1)")
        send_aa55_cmd(ser, CMD_MODE_TRAIN)
    elif ACTION == "test":
        print("[控制] 启动测试模式 (MI_INFER)")
        send_aa55_cmd(ser, CMD_MODE_TEST)
    elif ACTION == "ssvep":
        print(f"[控制] SSVEP启动 freq_idx={SSVEP_FREQ}")
        send_aa55_cmd(ser, CMD_SSVEP_START, bytes([SSVEP_FREQ]))
    elif ACTION == "ssvep_stop":
        print("[控制] SSVEP停止")
        send_aa55_cmd(ser, CMD_SSVEP_STOP)
    elif ACTION == "stop":
        print("[控制] 停止")
        send_aa55_cmd(ser, CMD_STOP)
    elif ACTION == "display":
        print(f"[控制] 显示模式 {DISPLAY_MODE} (0=SSVEP, 1=ARROW, 2=ARROW_TRAIN)")
        send_aa55_cmd(ser, CMD_DISPLAY_CFG, bytes([DISPLAY_MODE]))
    elif ACTION == "trial":
        print(f"[控制] 试次方向 {'LEFT' if TRIAL_SIDE == 0 else 'RIGHT'}")
        send_aa55_cmd(ser, CMD_TRIAL, bytes([TRIAL_SIDE]))
    elif ACTION == "mode_set":
        print(f"[控制] 模式设置 {COLLECT_MODE} (1=collect, 2=infer, 3=csp)")
        send_aa55_cmd(ser, CMD_MODE_SET, bytes([COLLECT_MODE]))
    elif ACTION == "monitor":
        print("[监控] 实时输出中 (Ctrl+C 退出)...")
        stop = threading.Event()
        t = threading.Thread(target=_log_reader, args=(ser, stop), daemon=True)
        t.start()
        try:
            while True:
                time.sleep(0.1)
        except KeyboardInterrupt:
            stop.set()
            print("\n[监控] 已停止")
    else:
        print(f"未知 ACTION: {ACTION}")
        print("可用: burn, train, test, ssvep, ssvep_stop, stop, display, trial, mode_set, monitor")

    if ACTION != "monitor":
        time.sleep(0.5)

    ser.close()


if __name__ == "__main__":
    main()
