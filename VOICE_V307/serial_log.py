"""VOICE 串口常驻监听 —— 自动识别新启动并轮转日志

用法:  python serial_log.py          (COM9@2M, Ctrl+C 停止)
行为:  一直挂在国际蓝桥/调试串口上收数据; 每次识别到新启动标记
      (main() 的第一行打印 "SystemClk:") 就删除旧日志文件, 用新启动
      的内容重建 —— com9_log.txt 永远只保留"最近一次启动"的全过程。
用途:  烧录前开着它, 烧录完成后日志自动就位, 无需手动截屏。
"""
import serial, time, os, sys

PORT = "COM9"
BAUD = 2000000
HERE = os.path.dirname(os.path.abspath(__file__))
LOG = os.path.join(HERE, "com9_log.txt")
BOOT_MARKER = b"SystemClk:"      # main() 启动第一行: SystemClk:144000000
CAP_LIMIT = 512 * 1024           # 单次启动日志上限(超长截断保留尾部)

def write_log(data):
    with open(LOG, "wb") as f:   # 覆盖写 = "删除旧日志"语义
        f.write(data)

def main():
    ser = serial.Serial(PORT, BAUD, timeout=0.5)
    buf = b""
    print(f"监听 {PORT}@{BAUD} -> {LOG}")
    print("识别到新启动(SystemClk)即轮转; Ctrl+C 停止")
    try:
        while True:
            chunk = ser.read(4096)
            if not chunk:
                if os.path.exists(LOG):
                    write_log(buf)       # 空闲期保底落盘(覆盖写)
                continue

            buf += chunk
            if len(buf) > CAP_LIMIT:
                buf = buf[-CAP_LIMIT:]   # 环形截断, 保尾部

            if BOOT_MARKER in chunk or BOOT_MARKER in buf:
                idx = buf.rfind(BOOT_MARKER)
                buf = buf[idx:]          # 旧启动内容丢弃(=删除旧日志)
                write_log(buf)
                print(f"[{time.strftime('%H:%M:%S')}] 检测到新启动, 日志已重置 ({len(buf)}B)")
            else:
                write_log(buf)           # 覆盖写当前启动日志
    except KeyboardInterrupt:
        pass
    finally:
        write_log(buf)
        ser.close()
        print("已停止")

if __name__ == "__main__":
    main()
