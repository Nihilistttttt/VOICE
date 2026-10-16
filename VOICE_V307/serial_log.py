"""VOICE 串口常驻监听 —— 自动识别新启动并轮转日志

用法:  python serial_log.py          (自动找 CH340, 2M, Ctrl+C 停止)
行为:  一直挂在调试串口上收数据; 每次识别到新启动标记
      (main() 的第一行打印 "SystemClk:") 就删除旧日志文件, 用新启动
      的内容重建 —— com9_log.txt 永远只保留"最近一次启动"的全过程。
健壮性: 端口号自动探测(按 CH340 VID:PID=1a86:7523, 重枚举换号不怕);
      烧录/复位瞬间断口自动重连; 日志缓冲跨断线保留。

轮转判定: rfind(标记)>0 才算新启动 —— 标记在缓冲起点=当前启动(不
动作); 标记再次出现在数据流中段=新一轮启动(轮转)。
"""
import serial, serial.tools.list_ports, time, os, sys

BAUD = 2000000
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "com9_log.txt")
BOOT_MARKER = b"SystemClk:"      # main() 启动第一行: SystemClk:144000000
CAP_LIMIT = 512 * 1024           # 单次启动日志上限(超长截断保留尾部)

def find_ch340():
    """按 VID:PID 找 CH340 (1a86:7523) —— 端口号随重枚举漂移, 不写死"""
    for p in serial.tools.list_ports.comports():
        if (p.vid or 0) == 0x1A86 and (p.pid or 0) == 0x7523:
            return p.device
    return None

def write_log(data):
    with open(LOG, "wb") as f:   # 覆盖写 = "删除旧日志"语义
        f.write(data)

def main():
    buf = b""
    ser = None
    print(f"监听 CH340@{BAUD} -> {LOG}")
    print("识别到新启动(SystemClk)即轮转; 断线/换号自动重连; Ctrl+C 停止")
    while True:
        try:
            if ser is None or not ser.is_open:
                port = find_ch340()
                if port is None:
                    time.sleep(1.0)
                    continue
                ser = serial.Serial(port, BAUD, timeout=0.5)
                print(f"[{time.strftime('%H:%M:%S')}] 串口已打开 ({port})")

            chunk = ser.read(4096)
            if not chunk:
                if os.path.exists(LOG) and buf:
                    write_log(buf)       # 空闲期保底落盘(覆盖写)
                continue

            buf += chunk
            if len(buf) > CAP_LIMIT:
                buf = buf[-CAP_LIMIT:]   # 环形截断, 保尾部

            idx = buf.rfind(BOOT_MARKER)
            if idx > 0:                  # 新启动: 标记不在缓冲起点
                buf = buf[idx:]          # 旧启动内容丢弃(=删除旧日志)
                write_log(buf)
                print(f"[{time.strftime('%H:%M:%S')}] 检测到新启动, 日志已重置 ({len(buf)}B)")
            else:
                write_log(buf)           # 覆盖写当前启动日志

        except serial.SerialException as e:
            # 断线(烧录/复位导致 USB 重枚举): 关闭重试, 缓冲保留
            print(f"[{time.strftime('%H:%M:%S')}] 串口断开({e}), 1s 后重连")
            try:
                if ser is not None:
                    ser.close()
            except Exception:
                pass
            ser = None
            time.sleep(1.0)
        except KeyboardInterrupt:
            break

    if ser is not None and ser.is_open:
        write_log(buf)
        ser.close()
    print("已停止")

if __name__ == "__main__":
    main()
