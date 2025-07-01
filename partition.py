
import argparse
from pathlib import Path

import pandas as pd
import numpy as np
from sklearn.model_selection import StratifiedKFold

# -------------- Argumentos CLI --------------
parser = argparse.ArgumentParser(description="Particionar CSV balanceado")
parser.add_argument("--csv",     required=True,  help="Ruta al dataset CSV")
parser.add_argument("--clients", required=True,  type=int,
                    help="Cantidad de particiones (N clientes)")
parser.add_argument("--prefix",  default="part",
                    help="Prefijo para archivos de salida (part0.csv, …)")

args = parser.parse_args()
csv_path   = Path(args.csv)
n_clients  = args.clients
prefix     = args.prefix

# -------------- Cargar dataset --------------
df = pd.read_csv(csv_path)
if df.shape[1] < 2:
    raise ValueError("El CSV debe tener al menos 2 columnas (features + target)")

y = df.iloc[:, -1].values  # última columna = target
X = df.iloc[:, :-1].values

# -------------- StratifiedKFold --------------
skf = StratifiedKFold(n_splits=n_clients, shuffle=True, random_state=42)

for k, (_, idx) in enumerate(skf.split(X, y)):
    part_df = df.iloc[idx]                        # incluye target
    out_csv = f"{prefix}{k}.csv"
    part_df.to_csv(out_csv, index=False)

    # Estadística rápida
    counts = part_df.iloc[:, -1].value_counts().to_dict()
    print(f"{out_csv:>12}  filas={len(part_df):>4}  balance={counts}")

print("Particionado balanceado finalizado")