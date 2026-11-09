"""Read USB logs and optionally exercise the local UI's serial page shortcuts."""
import argparse
from pathlib import Path
import time
import serial

parser=argparse.ArgumentParser()
parser.add_argument('--port',default='COM12')
parser.add_argument('--seconds',type=float,default=20)
parser.add_argument('--commands',default='')
parser.add_argument('--interval',type=float,default=1.5)
parser.add_argument('--log',default='build/monitor_ui.log')
args=parser.parse_args()
log_path=Path(args.log)
log_path.parent.mkdir(parents=True,exist_ok=True)
port=serial.Serial(port=None,baudrate=115200,timeout=0.2)
port.dtr=False
port.rts=False
port.port=args.port
port.open()
start=time.monotonic()
sent=0
try:
    with log_path.open('w',encoding='utf-8') as log:
        while time.monotonic()-start<args.seconds:
            if sent<len(args.commands) and time.monotonic()-start>0.5+sent*args.interval:
                command=args.commands[sent]
                port.write(command.encode('ascii'))
                marker=f'\n[HOST] command {command!r}\n'
                print(marker,end='',flush=True)
                log.write(marker)
                sent+=1
            data=port.read(max(1,port.in_waiting))
            if data:
                text=data.decode('utf-8',errors='replace')
                print(text,end='',flush=True)
                log.write(text)
                log.flush()
finally:
    port.close()
