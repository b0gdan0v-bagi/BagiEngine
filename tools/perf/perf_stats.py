"""The statistics of the perf runner: a mean with its 95 % interval, and the verdict of a run against a baseline. Standard library only.

The repetitions of a benchmark are few (5 by default), so the interval uses Student's t, not the 1.96 of the normal approximation that
the Mahjong arena (`ArenaStats`) can afford over hundreds of seeds. Two runs are compared by Welch's t interval of the difference of the
means, so a baseline measured with more or fewer repetitions still compares.
"""

import math
from dataclasses import dataclass

# Two-sided 95 % critical values of Student's t by degrees of freedom; above the table the value goes to the normal 1.96.
_T95 = {
    1: 12.706, 2: 4.303, 3: 3.182, 4: 2.776, 5: 2.571, 6: 2.447, 7: 2.365, 8: 2.306, 9: 2.262, 10: 2.228,
    11: 2.201, 12: 2.179, 13: 2.160, 14: 2.145, 15: 2.131, 16: 2.120, 17: 2.110, 18: 2.101, 19: 2.093, 20: 2.086,
    25: 2.060, 30: 2.042, 40: 2.021, 60: 2.000, 120: 1.980,
}
NORMAL_95 = 1.96

REGRESSION = "regression"
IMPROVEMENT = "improvement"
NOISE = "noise"
NEW = "new"  # no baseline for the metric


def t95(df: float) -> float:
    """The two-sided 95 % critical value of t for `df` degrees of freedom (fractional for Welch), interpolated in the table."""
    if df < 1:
        return _T95[1]
    keys = sorted(_T95)
    if df >= keys[-1]:
        return NORMAL_95 + (_T95[keys[-1]] - NORMAL_95) * keys[-1] / df
    for low, high in zip(keys, keys[1:]):
        if low <= df <= high:
            return _T95[low] + (_T95[high] - _T95[low]) * (df - low) / (high - low)
    return _T95[keys[0]]


@dataclass
class Summary:
    """A metric over the repetitions: `half` is the half-width of the 95 % interval (0 with fewer than 2 samples)."""

    n: int
    mean: float
    sd: float
    half: float

    def to_json(self) -> dict:
        return {"n": self.n, "mean": self.mean, "sd": self.sd, "half": self.half}

    @staticmethod
    def from_json(data: dict) -> "Summary":
        return Summary(int(data["n"]), float(data["mean"]), float(data["sd"]), float(data["half"]))


def summarize(samples: list[float]) -> Summary:
    n = len(samples)
    if n == 0:
        return Summary(0, 0.0, 0.0, 0.0)
    mean = sum(samples) / n
    if n < 2:
        return Summary(n, mean, 0.0, 0.0)
    sd = math.sqrt(sum((x - mean) ** 2 for x in samples) / (n - 1))
    return Summary(n, mean, sd, t95(n - 1) * sd / math.sqrt(n))


@dataclass
class Comparison:
    """`new - old` of the means, its 95 % half-width, the change in percent of the old mean and the verdict."""

    delta: float
    half: float
    percent: float
    verdict: str


def compare(new: Summary, old: Summary, better: str, threshold_percent: float) -> Comparison:
    """The verdict of `new` against the baseline `old` for a metric where `better` is "lower" or "higher".

    A difference is real when Welch's 95 % interval of `new - old` excludes 0 **and** the change is at least `threshold_percent` of the old
    mean: a machine drifts by a percent or two between days, which an interval of five runs of one day cannot see. Inside either bound it
    is noise. Two deterministic series (both sd 0) differ for real whenever their means differ by the threshold.
    """
    delta = new.mean - old.mean
    percent = 100.0 * delta / old.mean if old.mean else (0.0 if delta == 0 else math.copysign(math.inf, delta))
    var_new = new.sd**2 / new.n if new.n else 0.0
    var_old = old.sd**2 / old.n if old.n else 0.0
    se = math.sqrt(var_new + var_old)
    if se == 0:
        half = 0.0
    else:
        df_den = (var_new**2 / (new.n - 1) if new.n > 1 else 0.0) + (var_old**2 / (old.n - 1) if old.n > 1 else 0.0)
        df = (se**4 / df_den) if df_den > 0 else 1.0
        half = t95(df) * se
    significant = abs(delta) > half and abs(percent) >= threshold_percent
    if not significant:
        verdict = NOISE
    else:
        worse = delta > 0 if better == "lower" else delta < 0
        verdict = REGRESSION if worse else IMPROVEMENT
    return Comparison(delta, half, percent, verdict)


def benchmark_verdict(verdicts: list[str]) -> str:
    """The verdict of a benchmark from those of its gated metrics: any regression wins, then an improvement, then noise."""
    for verdict in (REGRESSION, IMPROVEMENT, NOISE):
        if verdict in verdicts:
            return verdict
    return NEW
