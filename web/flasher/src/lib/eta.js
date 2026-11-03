/** "About 4 minutes left" from progress so far. */

export function timeLeft(done, total, elapsedMs) {
  if (!(done > 0) || !(total > 0) || done >= total) return done >= total && total > 0 ? "Almost done…" : "Starting…";
  const leftMs = (elapsedMs / done) * (total - done);
  const s = Math.round(leftMs / 1000);
  if (s < 60) return s <= 5 ? "A few seconds left" : `About ${s} seconds left`;
  const m = Math.round(s / 60);
  return m === 1 ? "About 1 minute left" : `About ${m} minutes left`;
}
