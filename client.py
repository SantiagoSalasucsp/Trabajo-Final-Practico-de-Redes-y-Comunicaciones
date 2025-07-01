#!/usr/bin/env python3
import socket, re, numpy as np, pandas as pd, torch, torch.nn as nn
import torch.nn.functional as F
from torch.utils.data import DataLoader, TensorDataset
import tcp_cpp
import textwrap


np.set_printoptions(precision=15, linewidth=120)
# ───────── CONFIG ─────────
HOST, PORT = "127.0.0.1", 9000
# cabecera: 10 díg size + 1 byte type   (M/m añaden 1 byte layer)
HDR_NO_LAY = 11

# ───── helpers TCP ─────
def make_hdr(size: int, typ: str, layer: str | None = None) -> bytes:
    return f"{size:010d}{typ}{layer or ''}".encode()

def send_bytes(sock: socket.socket, b: bytes):
    tcp_cpp.send_message(sock.fileno(), b)

def recv_exact(sock: socket.socket, n: int) -> bytes:
    return tcp_cpp.receive_exact(sock.fileno(), n)

def recv_pkt(sock):
    h = recv_exact(sock, HDR_NO_LAY)
    size, mtype = int(h[:10]), h[10:11]

    if mtype == b'z':
        print("Servidor canceló: timeout.")
        sock.close(); exit(1)

    layer = None
    if mtype in (b'M', b'm'):
        layer = recv_exact(sock, 1)
    payload = recv_exact(sock, size) if size else b''
    return mtype, layer, payload

# ───── helpers print ─────
def printM(header: bytes, payload: bytes, preview: int = 32):
    print("\n──────── Mensaje M ────────")
    print(f"Header ASCII      : {header.decode()}  "
          f"({len(header)} bytes)")

    if payload:
        shown = payload[:preview]
        print(f"Payload preview   : {len(shown)}/{len(payload)} bytes "
              f"(hex)")
        lines = textwrap.wrap(shown.hex(' '), 16*3)   # 16 bytes por línea
        for ln in lines:
            print("  " + ln)
        if len(payload) > preview:
            print("  … (continua) …")
    else:
        print("Payload preview   : <vacío>")
    print("─────────────────────────────\n")

def printMValores(header: bytes,
                       arr: np.ndarray,
                       sample: int = 10):
    flat = arr.ravel()                       # 1-D view
    n     = flat.size
    k     = min(sample, n)

    print("\n──────── Mensaje M(valores reales) ────────")
    print(f"Header ASCII      : {header.decode()}  ({len(header)} bytes)")
    print(f"Matriz shape      : {arr.shape}  |  elementos = {n}")

    print(f"Primeros {k} valores (float64):")
    print("  ", "  ".join(f"{v: .15f}" for v in flat[:k]))
    if k < n:
        print("  …")
    print("──────────────────────────────\n")

# ───────── conectar ─────────
sock = socket.socket(); sock.connect((HOST, PORT))

# ── Solicitar datos ──
send_bytes(sock, make_hdr(0, 'I'))

t, _, p = recv_pkt(sock);  assert t == b'i'
cid = int(p.decode())

print(f"Id recibido [C{cid}]")

# ───────── SETUP (e, i, f) ─────────
t, _, p = recv_pkt(sock);  assert t == b'e'
epochs = int(p.decode())

t, _, p = recv_pkt(sock);  assert t == b'f'
csv = p.decode()

print(f"[C{cid}] epochs={epochs}, file={csv}")

# ───────── esperar start ─────────
assert recv_pkt(sock)[0] == b's'
print(f"[C{cid}] Comenzando entrenamiento")

# ───────── datos ─────────
df  = pd.read_csv(csv)
Xnp = df.iloc[:, :-1].values.astype(np.float32)
ynp = df.iloc[:, -1].values.astype(np.float32)
dl  = DataLoader(TensorDataset(torch.tensor(Xnp),
                               torch.tensor(ynp)),
                 batch_size=32, shuffle=True)

# ───────── modelo ─────────
class Net(nn.Module):
    def __init__(self):
        super().__init__()
        self.fc1 = nn.Linear(17, 64)
        self.fc2 = nn.Linear(64, 32)
        self.o   = nn.Linear(32, 1)
    def forward(self, x):
        return self.o(F.relu(self.fc2(F.relu(self.fc1(x))))).squeeze(1)

net = Net()
opt   = torch.optim.Adam(net.parameters(), 1e-3)
lossf = nn.BCEWithLogitsLoss()
layers = [net.fc1, net.fc2, net.o]

# ───────── entrenamiento ─────────
for ep in range(epochs):
    net.train()
    tot, batches = 0.0, 0
    for xb, yb in dl:
        opt.zero_grad()
        loss = lossf(net(xb), yb)
        loss.backward(); opt.step()
        tot += loss.item(); batches += 1
    avg_loss = tot / batches

    # sincr. pesos
    for lid, layer in enumerate(layers):
        # Enviar matriz (mensaje M)
        w = layer.weight.data.cpu().numpy().astype(np.float64)
        header  = make_hdr(w.size * 8, 'M', str(lid))
        payload = w.tobytes()
        #printM(header, payload, preview=64)
        printMValores(header, w, sample=8) 
        send_bytes(sock, header + payload) 

        # Recibir matriz promedio (mensaje m)
        typ, lay, pay = recv_pkt(sock)
        assert typ == b'm' and int(lay) == lid
        avg_np = np.frombuffer(pay, np.float64).reshape(w.shape).copy()
        #actualizar pesos
        layer.weight.data.copy_(torch.from_numpy(avg_np).float())

    print(f"[C{cid}] epoch {ep+1}/{epochs} | loss = {avg_loss:.4f}")

# ───────── fin ─────────
assert recv_pkt(sock)[0] == b'x'   # mensaje de fin (x)
sock.close()
print(f"[C{cid}] terminado")