//! Genetic-algorithm matcher (the orchestration layer).
//!
//! The GA searches the space of *assignments*: each source symbol picks one
//! candidate target function. Fitness combines:
//!   - per-pair layer scores (pattern / CFG / strings / constants / size)
//!   - a global callee-consistency term: when a source function calls another
//!     *known source symbol*, a candidate that calls that symbol's assigned
//!     target is rewarded. This is the xref layer made emergent — weak single
//!     pairs resolve through the community of already-consistent matches.
//!   - collision penalties (two symbols may not claim the same target).
//!
//! Deterministic (seeded) RNG so tests and CI are reproducible.

use crate::corpus::{Corpus, FunctionRecord};
use crate::score::score_pair;

// ---------------------------------------------------------------------------
// Deterministic RNG (xorshift64*), small and testable.
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct Rng(u64);

impl Rng {
    pub fn new(seed: u64) -> Self {
        Rng(if seed == 0 { 0x9E3779B97F4A7C15 } else { seed })
    }
    fn next(&mut self) -> u64 {
        let mut x = self.0;
        x ^= x >> 12;
        x ^= x << 25;
        x ^= x >> 27;
        self.0 = x;
        x.wrapping_mul(0x2545F4914F6CDD1D)
    }
    /// Uniform integer in [0, n).
    pub fn below(&mut self, n: usize) -> usize {
        if n == 0 {
            return 0;
        }
        (self.next() % n as u64) as usize
    }
    pub fn chance(&mut self, p: f64) -> bool {
        (self.next() as f64 / u64::MAX as f64) < p
    }
}

// ---------------------------------------------------------------------------
// Candidate pools
// ---------------------------------------------------------------------------

/// One candidate target for a source symbol.
#[derive(Debug, Clone)]
pub struct Candidate {
    /// Index into the target corpus functions.
    pub target_idx: usize,
    /// Structural layer score (pattern/cfg/strings/const/size).
    pub score: f64,
    /// Anchor xref bonus (callee + caller overlap with known anchors).
    /// Used only to break structural ties; never overrides a structural gap.
    pub xref: f64,
}

/// An established source->target mapping used to seed callee-consistency.
#[derive(Debug, Clone)]
pub struct Anchor {
    pub source_addr: u64,
    pub target_addr: u64,
}

/// The search problem for one source symbol.
#[derive(Debug, Clone)]
pub struct SymbolProblem {
    /// Index into source corpus functions.
    pub source_idx: usize,
    /// Semantic name from the source target pack (e.g. `lv_timer_create`),
    /// distinct from the corpus's auto `sub_...` name.
    pub name: String,
    /// Candidate pool (already pruned + pre-scored).
    pub candidates: Vec<Candidate>,
}

/// Callee-overlap bonus: for each source callee that is an anchor mapped to
/// target `T`, a candidate whose target function also calls `T` earns
/// `weight`. This is the xref layer made iterative: round 1 anchors seed
/// round 2+ scores.
pub fn anchor_callee_bonus(
    src: &FunctionRecord,
    dst: &FunctionRecord,
    anchors: &[Anchor],
    weight: f64,
) -> f64 {
    if anchors.is_empty() || src.callees.is_empty() {
        return 0.0;
    }
    let mut bonus = 0.0;
    for callee in &src.callees {
        let ca = callee_addr(callee);
        if let Some(a) = anchors.iter().find(|a| a.source_addr == ca) {
            if dst.callees.iter().any(|c| callee_addr(c) == a.target_addr) {
                bonus += weight;
            }
        }
    }
    bonus
}

/// Caller-overlap bonus (the reverse xref direction): for each source *caller*
/// that is an anchor mapped to target `T`, a candidate whose target function is
/// called by `T` earns `weight`. Tiny veneers (LDR.W PC,=literal) jump to RAM
/// aliases outside the XIP image, so their callee set is unusable; their
/// identity lives in which real functions call them.
pub fn anchor_caller_bonus(
    src: &FunctionRecord,
    dst: &FunctionRecord,
    anchors: &[Anchor],
    weight: f64,
) -> f64 {
    if anchors.is_empty() || src.callers.is_empty() {
        return 0.0;
    }
    let mut bonus = 0.0;
    for caller in &src.callers {
        let ca = callee_addr(caller);
        if let Some(a) = anchors.iter().find(|a| a.source_addr == ca) {
            if dst.callers.iter().any(|c| callee_addr(c) == a.target_addr) {
                bonus += weight;
            }
        }
    }
    bonus
}

/// Build candidate pools by filtering the target corpus with coarse,
/// relocation-robust predicates, then scoring the survivors. `source` is a
/// list of (source corpus index, semantic name) pairs.
pub fn build_pools(
    source: &[(usize, String)],
    src_corpus: &Corpus,
    dst_corpus: &Corpus,
    max_pool: usize,
    anchors: &[Anchor],
) -> Vec<SymbolProblem> {
    // Index target by size bucket for cheap pruning.
    let mut by_size: Vec<usize> = (0..dst_corpus.functions.len()).collect();
    by_size.sort_by_key(|&i| dst_corpus.functions[i].size);

    source
        .iter()
        .map(|&(si, ref name)| {
            let sf = &src_corpus.functions[si];
            let mut cands: Vec<Candidate> = Vec::new();
            // Size window: [size/3, size*3] and block-count within +-50%.
            let lo = sf.size / 3;
            let hi = sf.size * 3;
            for &ti in &by_size {
                let tf = &dst_corpus.functions[ti];
                if tf.size < lo || tf.size > hi {
                    if tf.size > hi {
                        break; // sorted by size -> can stop early
                    }
                    continue;
                }
                // Block-count similarity (both trivial or within 2x).
                if sf.blocks != 0 || tf.blocks != 0 {
                    let (a, b) = (
                        sf.blocks.min(tf.blocks) as f64,
                        sf.blocks.max(tf.blocks) as f64,
                    );
                    if a == 0.0 || (b / a) > 2.0 {
                        continue;
                    }
                }
                let s = score_pair(sf, tf);
                // Gate: weak pairs never enter the pool.
                if s.composite() < 1.5 {
                    continue;
                }
                // Anchor xref (callee + caller overlap) is a *tiebreaker* only:
                // it breaks ties among structurally-equal candidates (e.g. the
                // many identical 8-byte veneers) without ever overriding a
                // structural gap.
                let xref = anchor_callee_bonus(sf, tf, anchors, 6.0)
                    + anchor_caller_bonus(sf, tf, anchors, 4.0);
                cands.push(Candidate {
                    target_idx: ti,
                    score: s.composite(),
                    xref,
                });
            }
            // Sort by structural score first; xref breaks exact ties.
            cands.sort_by(|a, b| {
                b.score
                    .partial_cmp(&a.score)
                    .unwrap()
                    .then(b.xref.partial_cmp(&a.xref).unwrap())
            });
            cands.truncate(max_pool);
            SymbolProblem {
                source_idx: si,
                name: name.clone(),
                candidates: cands,
            }
        })
        .collect()
}

// ---------------------------------------------------------------------------
// Genetic algorithm
// ---------------------------------------------------------------------------

#[derive(Debug, Clone)]
pub struct GaParams {
    pub population: usize,
    pub generations: usize,
    pub mutation_rate: f64,
    pub elitism: usize,
    /// Weight of the global callee-consistency term.
    pub callee_weight: f64,
    /// Penalty per shared-target collision.
    pub collision_penalty: f64,
}

impl Default for GaParams {
    fn default() -> Self {
        GaParams {
            population: 96,
            generations: 240,
            mutation_rate: 0.10,
            elitism: 4,
            callee_weight: 1.0,
            collision_penalty: 10.0,
        }
    }
}

/// A chromosome: for each symbol problem, an index into its candidate pool,
/// or None if unassigned.
#[derive(Debug, Clone)]
pub struct Individual {
    pub genes: Vec<Option<usize>>,
    pub fitness: f64,
}

fn callee_addr(s: &str) -> u64 {
    u64::from_str_radix(s.trim_start_matches("0x"), 16).unwrap_or(0)
}

/// Fitness of an individual: sum of structural pair scores + tiebreaking xref
/// - collision penalties.
fn evaluate(indiv: &mut Individual, pools: &[SymbolProblem], params: &GaParams) -> f64 {
    let mut fitness = 0.0;
    let mut claimed: std::collections::HashMap<usize, usize> = std::collections::HashMap::new();
    for (i, pool) in pools.iter().enumerate() {
        let Some(gi) = indiv.genes[i] else {
            continue;
        };
        let Some(cand) = pool.candidates.get(gi) else {
            continue;
        };
        fitness += cand.score;
        // Tiebreaking xref (scaled far below structural): preserves the
        // pool's (score, xref) ordering under GA mutation.
        fitness += cand.xref * 0.001;
        *claimed.entry(cand.target_idx).or_insert(0) += 1;
    }
    for (_, count) in claimed {
        if count > 1 {
            fitness -= params.collision_penalty * (count as f64 - 1.0);
        }
    }
    indiv.fitness = fitness;
    fitness
}

/// Run the GA and return the best individual.
pub fn run_ga(pools: &[SymbolProblem], params: &GaParams, seed: u64) -> Individual {
    let mut rng = Rng::new(seed);

    // Locked genes: symbols whose pool-top beats the runner-up by a decisive
    // margin are fixed to their best structural candidate. The GA must never
    // drift a confident match onto a weaker one through mutation noise; it
    // only searches the genuinely ambiguous (tied) symbols.
    let locked: Vec<bool> = pools
        .iter()
        .map(|p| {
            if p.candidates.len() < 2 {
                return true;
            }
            let best = p.candidates[0].score;
            let runner = p.candidates[1].score;
            best > 0.0 && (best - runner) / best > 0.25
        })
        .collect();

    let greedy: Vec<Option<usize>> = pools
        .iter()
        .map(|p| {
            if p.candidates.is_empty() {
                None
            } else {
                Some(0)
            }
        })
        .collect();

    // Population seeded near the greedy baseline: every individual starts from
    // the per-symbol best, with `k` random genes perturbed. This keeps the
    // whole population in the good region of the landscape.
    let mut pop: Vec<Individual> = Vec::with_capacity(params.population);
    for k in 0..params.population {
        let mut genes = greedy.clone();
        // Perturb a handful of *unlocked* genes (never locked ones).
        let perturb = k % 3; // 0..2 random genes from baseline
        for _ in 0..perturb {
            let i = rng.below(pools.len());
            if locked[i] || pools[i].candidates.is_empty() {
                continue;
            }
            genes[i] = Some(rng.below(pools[i].candidates.len()));
        }
        let mut indiv = Individual {
            genes,
            fitness: 0.0,
        };
        evaluate(&mut indiv, pools, params);
        pop.push(indiv);
    }

    for _ in 0..params.generations {
        // Tournament selection -> next generation.
        let mut next: Vec<Individual> = Vec::with_capacity(params.population);
        // Elitism: keep the best few unchanged.
        pop.sort_by(|a, b| b.fitness.partial_cmp(&a.fitness).unwrap());
        for e in 0..params.elitism.min(pop.len()) {
            next.push(pop[e].clone());
        }
        while next.len() < params.population {
            let p1 = tournament(&pop, &mut rng);
            let p2 = tournament(&pop, &mut rng);
            let mut child = crossover(&p1, &p2, &mut rng);
            mutate(&mut child, pools, &locked, &mut rng, params.mutation_rate);
            evaluate(&mut child, pools, params);
            next.push(child);
        }
        pop = next;
    }

    pop.sort_by(|a, b| b.fitness.partial_cmp(&a.fitness).unwrap());
    pop.remove(0)
}

fn tournament(pop: &[Individual], rng: &mut Rng) -> Individual {
    let a = &pop[rng.below(pop.len())];
    let b = &pop[rng.below(pop.len())];
    if a.fitness >= b.fitness {
        a.clone()
    } else {
        b.clone()
    }
}

fn crossover(a: &Individual, b: &Individual, rng: &mut Rng) -> Individual {
    let n = a.genes.len();
    let mut genes = vec![None; n];
    for i in 0..n {
        genes[i] = if rng.chance(0.5) {
            a.genes[i]
        } else {
            b.genes[i]
        };
    }
    Individual {
        genes,
        fitness: 0.0,
    }
}

fn mutate(
    indiv: &mut Individual,
    pools: &[SymbolProblem],
    locked: &[bool],
    rng: &mut Rng,
    rate: f64,
) {
    for i in 0..indiv.genes.len() {
        if locked[i] {
            continue; // never drift a confident match
        }
        if rng.chance(rate) {
            let p = &pools[i];
            if p.candidates.is_empty() {
                indiv.genes[i] = None;
            } else {
                indiv.genes[i] = Some(rng.below(p.candidates.len()));
            }
        }
    }
}

// ---------------------------------------------------------------------------
// Output
// ---------------------------------------------------------------------------

/// Final match report for one symbol.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct MatchResult {
    pub name: String,
    pub source_addr: String,
    /// Best candidate target address (hex string), if any.
    pub target_addr: Option<String>,
    pub target_name: Option<String>,
    pub score: Option<f64>,
    /// How many distinct target functions were plausible (pool size at end).
    pub pool_size: usize,
    /// Margin: (best - second) / best, higher = more decisive.
    pub margin: Option<f64>,
}

/// Produce the final report: for each symbol, the assigned target plus the
/// runner-up margin.
pub fn finalize(
    best: &Individual,
    pools: &[SymbolProblem],
    src_corpus: &Corpus,
    dst_corpus: &Corpus,
) -> Vec<MatchResult> {
    let mut out = Vec::with_capacity(pools.len());
    for (i, pool) in pools.iter().enumerate() {
        let src = &src_corpus.functions[pool.source_idx];
        let mut result = MatchResult {
            name: pool.name.clone(),
            source_addr: src.addr.clone(),
            target_addr: None,
            target_name: None,
            score: None,
            pool_size: pool.candidates.len(),
            margin: None,
        };
        if let Some(gi) = best.genes[i]
            && let Some(cand) = pool.candidates.get(gi)
        {
            let tf = &dst_corpus.functions[cand.target_idx];
            result.target_addr = Some(tf.addr.clone());
            result.target_name = Some(tf.name.clone());
            result.score = Some(cand.score);
            // Margin vs the runner-up candidate in the pool (if any). A single
            // candidate pool is decisive *by absence of competition*: there
            // is no alternative target passing the gates, so margin is 1.0.
            let runner = pool
                .candidates
                .iter()
                .filter(|c| c.target_idx != cand.target_idx)
                .map(|c| c.score)
                .max_by(|a, b| a.partial_cmp(b).unwrap());
            if let Some(r) = runner
                && cand.score > 0.0
            {
                result.margin = Some((cand.score - r) / cand.score);
            } else if pool.candidates.len() == 1 {
                result.margin = Some(1.0);
            }
        }
        out.push(result);
    }
    out
}

/// A matched function record, suitable for downstream symbol creation.
#[derive(Debug, Clone, serde::Serialize, serde::Deserialize)]
pub struct ConfirmedMatch {
    pub name: String,
    pub source_addr: String,
    pub target_addr: String,
    pub score: f64,
    pub margin: f64,
    pub target_name: String,
}

/// Functions in the target corpus at given addresses (for lookups).
pub fn function_by_addr<'c>(corpus: &'c Corpus, addr: u64) -> Option<&'c FunctionRecord> {
    corpus.function_at(addr)
}

#[cfg(test)]
mod tests {
    use super::*;
    use crate::corpus::{BlockShape, FunctionRecord};

    fn rec(addr: &str, size: u64, insn: u64, entry: &str) -> FunctionRecord {
        let blocks = 2;
        FunctionRecord {
            addr: addr.into(),
            name: String::new(),
            size,
            insn,
            blocks,
            block_offs: vec![
                BlockShape { off: 0, size: 8 },
                BlockShape {
                    off: 8,
                    size: size.saturating_sub(8).max(8),
                },
            ],
            succ: vec![(0, 1)],
            callees: vec![],
            callers: vec![],
            strings: vec![],
            constants: vec![],
            entry: entry.into(),
            data_refs: vec![],
        }
    }

    fn corpus(id: &str, funcs: Vec<FunctionRecord>) -> Corpus {
        Corpus {
            schema: 1,
            target_id: id.into(),
            image_base: "0x0".into(),
            functions: funcs,
            globals: vec![],
        }
    }

    #[test]
    fn rng_deterministic() {
        let mut a = Rng::new(42);
        let mut b = Rng::new(42);
        for _ in 0..100 {
            assert_eq!(a.below(1000), b.below(1000));
        }
    }

    #[test]
    fn pools_find_exact_duplicate() {
        // Source function identical bytes to one target function, different address.
        let src = corpus("s", vec![rec("0x1000", 64, 32, "00f0b500be00bd")]);
        let dst = corpus(
            "d",
            vec![
                rec("0x9000", 64, 32, "00f0b500be00bd"), // exact duplicate
                rec("0xA000", 200, 90, "00e40000f0b5"),  // different
                rec("0xB000", 64, 30, "00f0b500be00bf"), // near (1 byte diff)
            ],
        );
        let pools = build_pools(&[(0, "foo".into())], &src, &dst, 8, &[]);
        assert_eq!(pools.len(), 1);
        assert_eq!(pools[0].candidates.len(), 2); // duplicate + near, not the 200-byte one
        assert_eq!(pools[0].candidates[0].target_idx, 0);
        assert!(pools[0].candidates[0].score > pools[0].candidates[1].score);
    }

    #[test]
    fn ga_assigns_exact_duplicate() {
        let src = corpus("s", vec![rec("0x1000", 64, 32, "00f0b500be00bd")]);
        let dst = corpus(
            "d",
            vec![
                rec("0x9000", 64, 32, "00f0b500be00bd"),
                rec("0xA000", 64, 30, "00f0b500be00bf"),
                rec("0xB000", 200, 90, "00e40000f0b5"),
            ],
        );
        let pools = build_pools(&[(0, "foo".into())], &src, &dst, 8, &[]);
        let best = run_ga(&pools, &GaParams::default(), 7);
        assert_eq!(best.genes[0], Some(0));
    }

    #[test]
    fn ga_avoids_collision() {
        // Two source functions that both look like the same target; the GA
        // must map them to distinct targets even though one is a closer match.
        let src = corpus(
            "s",
            vec![
                rec("0x1000", 64, 32, "00f0b500be00bd"),
                rec("0x2000", 64, 31, "00f0b500be00bd"),
            ],
        );
        let dst = corpus(
            "d",
            vec![
                rec("0x9000", 64, 32, "00f0b500be00bd"), // exact for src[0]
                rec("0x9100", 64, 31, "00f0b500be00be"), // near for both
            ],
        );
        let pools = build_pools(&[(0, "a".into()), (1, "b".into())], &src, &dst, 8, &[]);
        let best = run_ga(&pools, &GaParams::default(), 7);
        let mut claimed: Vec<usize> = best.genes.iter().filter_map(|g| *g).collect();
        claimed.sort_unstable();
        let n = claimed.len();
        claimed.dedup();
        assert_eq!(claimed.len(), n, "collision should be penalized away");
    }
}
