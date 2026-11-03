import serial
import serial.tools.list_ports
import threading
import time
import sys

class SoapStationController:
    def __init__(self, port: str, baudrate: int = 115200):
        try:
            self.ser = serial.Serial(port, baudrate, timeout=1)
            self.running = True
            print(f"[系統訊息] 成功開啟通訊埠: {port}")
        except serial.SerialException as e:
            print(f"[錯誤] 無法開啟通訊埠 {port}: {e}")
            sys.exit(1)

        self.rx_thread = threading.Thread(target=self._receive_loop, daemon=True)
        self.rx_thread.start()

    def _receive_loop(self):
        while self.running:
            try:
                if self.ser.in_waiting > 0:
                    line = self.ser.readline().decode('utf-8', errors='ignore').strip()
                    if line:
                        print(f"\n[STM32 回傳] {line}")
            except Exception as e:
                if self.running:
                    print(f"\n[錯誤] 串口讀取異常: {e}")
                break
            time.sleep(0.02)

    def send_command(self, cmd: str):
        if self.ser and self.ser.is_open:
            full_cmd = cmd.strip() + "\r\n"
            self.ser.write(full_cmd.encode('utf-8'))
            print(f"[指令已送出]: {cmd.strip()}")

    def close(self):
        self.running = False
        if self.ser and self.ser.is_open:
            self.ser.close()
        print("\n[系統訊息] 串口連線已關閉。")

def auto_detect_port():
    ports = serial.tools.list_ports.comports()
    for p in ports:
        if "STMicroelectronics" in p.description or "STLink" in p.description or "STM" in p.description:
            return p.device
    return ports[0].device if ports else None

def display_menu():
    print("\n" + "="*68)
    print("  STM32 智慧肥皂攪拌工作站 (s, h 均採用 0~100% 百分比控制)")
    print("="*68)
    print("  [單獨設定]  m1=t,s%,h%  (例如: m1=6,15,20 ➔ 6秒, 綠燈15%, 加熱20%)")
    print("              m2=t,s%,h%  (例如: m2=6,50,60 ➔ 6秒, 綠燈50%, 加熱60%)")
    print("              m3=t,s%,h%  (例如: m3=6,100,100 ➔ 6秒, 綠燈100%, 加熱100%)")
    print("  [全套設定]  all=t1,s1%,h1%,t2,s2%,h2%,t3,s3%,h3%")
    print("              (例如: all=6,15,20,6,50,60,6,100,100)")
    print("  [S] 啟動自動工程流程 (MODE1 ➔ MODE2 ➔ MODE3 ➔ IDLE)")
    print("  [0] 手動中斷停止     (返回 IDLE / 關閉加熱)")
    print("  [E] 軟體急停         (EMERGENCY STOP)")
    print("  [R] 解除急停         (Reset Emergency)")
    print("  [Q] 離開控制台")
    print("="*68)

if __name__ == "__main__":
    target_port = auto_detect_port()
    if not target_port:
        target_port = input("未自動偵測到 ST-Link，請輸入 Port (如 COM3): ").strip()

    controller = SoapStationController(port=target_port)
    display_menu()

    try:
        while True:
            cmd = input("\n請輸入指令 > ").strip()
            if not cmd:
                continue

            if cmd.upper() == 'Q':
                break
            else:
                controller.send_command(cmd)
                time.sleep(0.3)
                display_menu()

    except KeyboardInterrupt:
        print("\n[系統訊息] 使用者中斷程式。")
    finally:
        controller.close()