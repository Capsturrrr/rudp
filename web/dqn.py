"""Deep Q-network for the window controller, in plain numpy (no torch needed to run or train).
Input: 5 continuous features. Output: one Q-value per window action (same 5 actions as the table agent)."""
import numpy as np, json

N_IN, N_H, N_OUT = 5, 32, 5

class DQN:
    def __init__(self, seed=0, lr=1e-3, gamma=0.9, buf=30000, batch=64):
        r = np.random.default_rng(seed)
        self.rng = r
        self.p = {"W1": r.normal(0, np.sqrt(2 / N_IN), (N_IN, N_H)), "b1": np.zeros(N_H),
                  "W2": r.normal(0, np.sqrt(2 / N_H), (N_H, N_H)), "b2": np.zeros(N_H),
                  "W3": r.normal(0, 0.05, (N_H, N_OUT)), "b3": np.zeros(N_OUT)}
        self.t = {k: v.copy() for k, v in self.p.items()}
        self.m = {k: np.zeros_like(v) for k, v in self.p.items()}
        self.v = {k: np.zeros_like(v) for k, v in self.p.items()}
        self.lr, self.gamma, self.batch, self.cap = lr, gamma, batch, buf
        self.S = np.zeros((buf, N_IN)); self.A = np.zeros(buf, dtype=int); self.R = np.zeros(buf)
        self.S2 = np.zeros((buf, N_IN)); self.n = 0; self.i = 0; self.steps = 0; self.sync = 400

    @staticmethod
    def fwd(p, x):
        h1 = np.maximum(0, x @ p["W1"] + p["b1"]); h2 = np.maximum(0, h1 @ p["W2"] + p["b2"])
        return h2 @ p["W3"] + p["b3"], h1, h2

    def q(self, x): return self.fwd(self.p, np.asarray(x, float))[0]

    def store(self, s, a, r, s2):
        i = self.i
        self.S[i], self.A[i], self.R[i], self.S2[i] = s, a, r, s2
        self.i = (i + 1) % self.cap; self.n = min(self.n + 1, self.cap)

    def train_step(self):
        if self.n < 256: return
        idx = self.rng.integers(0, self.n, self.batch)
        s, a, r, s2 = self.S[idx], self.A[idx], self.R[idx], self.S2[idx]
        qn = self.fwd(self.t, s2)[0]
        y = r + self.gamma * qn.max(1)
        q, h1, h2 = self.fwd(self.p, s)
        err = np.clip(q[np.arange(self.batch), a] - y, -1, 1)          # Huber gradient
        g3 = np.zeros_like(q); g3[np.arange(self.batch), a] = err / self.batch
        g = {"W3": h2.T @ g3, "b3": g3.sum(0)}
        d2 = (g3 @ self.p["W3"].T) * (h2 > 0)
        g["W2"], g["b2"] = h1.T @ d2, d2.sum(0)
        d1 = (d2 @ self.p["W2"].T) * (h1 > 0)
        g["W1"], g["b1"] = s.T @ d1, d1.sum(0)
        self.steps += 1
        for k in self.p:
            self.m[k] = 0.9 * self.m[k] + 0.1 * g[k]; self.v[k] = 0.999 * self.v[k] + 0.001 * g[k] ** 2
            mh = self.m[k] / (1 - 0.9 ** self.steps); vh = self.v[k] / (1 - 0.999 ** self.steps)
            self.p[k] -= self.lr * mh / (np.sqrt(vh) + 1e-8)
        if self.steps % self.sync == 0: self.t = {k: v.copy() for k, v in self.p.items()}

    def save(self, path):
        json.dump({k: v.tolist() for k, v in self.p.items()}, open(path, "w"))

    def load(self, path):
        d = json.load(open(path))
        self.p = {k: np.array(v) for k, v in d.items()}; self.t = {k: v.copy() for k, v in self.p.items()}
