#include "nsga_engine.hpp"
#include <functional>
#include <numeric>
#include <array>
#include <cmath>

NSGA2Engine::NSGA2Engine(
    const NSGASettings& nsga_settings,
    const std::vector<int>& role_ids,
    const std::unordered_map<int, RoleSettings>& role_settings,
    int players_in_team,
    const EngineSettings& engine_settings
) : nsga_settings_(nsga_settings),
    engine_settings_(engine_settings),
    role_ids_(role_ids),
    role_settings_(role_settings),
    players_in_team_(players_in_team),
    rng_(engine_settings.seed)
{
    num_roles_ = static_cast<int>(role_ids_.size());
    num_workers_ = engine_settings_.num_workers <= 0
        ? static_cast<int>(std::thread::hardware_concurrency())
        : engine_settings_.num_workers;
    if (num_workers_ <= 0) num_workers_ = engine_settings_.fallback_workers;

    team_slots_.reserve(players_in_team_);
    for (int r_idx = 0; r_idx < num_roles_; ++r_idx) {
        auto it = role_settings_.find(role_ids_[r_idx]);
        int count = (it != role_settings_.end()) ? it->second.count_in_team : 1;
        for (int c = 0; c < count; ++c) {
            team_slots_.push_back(r_idx);
        }
    }

    std::unordered_map<int, std::vector<int>> role_to_local;
    for (int local_idx = 0; local_idx < players_in_team_; ++local_idx) {
        role_to_local[team_slots_[local_idx]].push_back(local_idx);
    }
    for (auto& [role_idx, indices] : role_to_local) {
        if (indices.size() > 1) {
            dup_role_groups_.push_back(indices);
        }
    }
}

void NSGA2Engine::build_matrices(const std::vector<PlayerInfo>& players) {
    num_players_ = static_cast<int>(players.size());
    num_teams_ = num_players_ / players_in_team_;

    R_.assign(num_players_, std::vector<int>(num_roles_, 0));
    P_.assign(num_players_, std::vector<int>(num_roles_, 0));

    for (int i = 0; i < num_players_; ++i) {
        for (int j = 0; j < num_roles_; ++j) {
            R_[i][j] = players[i].get_rating_for_role(role_ids_[j]);
            P_[i][j] = players[i].get_priority_for_role(role_ids_[j]);
        }
    }

    priority_penalties_ = {
        nsga_settings_.penalty_invalid_role,
        nsga_settings_.penalty_prio_1,
        nsga_settings_.penalty_prio_2,
        nsga_settings_.penalty_prio_3
    };
}

std::vector<int> NSGA2Engine::generate_individual() {
    std::vector<int> individual(num_players_, 0);
    std::vector<bool> used(num_players_, false);

    std::uniform_int_distribution<int> dist(0, num_players_ - 1);

    for (int t = 0; t < num_teams_; ++t) {
        for (int s = 0; s < players_in_team_; ++s) {
            int slot_idx = t * players_in_team_ + s;
            int role_idx = team_slots_[s];

            std::vector<int> candidates;
            for (int p = 0; p < num_players_; ++p) {
                if (!used[p] && P_[p][role_idx] > 0) {
                    candidates.push_back(p);
                }
            }

            int chosen;
            if (!candidates.empty()) {
                std::uniform_int_distribution<int> cdist(0, static_cast<int>(candidates.size()) - 1);
                chosen = candidates[cdist(rng_)];
            } else {
                std::vector<int> remaining;
                for (int p = 0; p < num_players_; ++p) {
                    if (!used[p]) remaining.push_back(p);
                }
                std::uniform_int_distribution<int> rdist(0, static_cast<int>(remaining.size()) - 1);
                chosen = remaining[rdist(rng_)];
            }

            individual[slot_idx] = chosen;
            used[chosen] = true;
        }
    }

    return individual;
}

std::vector<std::array<float, 2>> NSGA2Engine::evaluate_population(
    const std::vector<std::vector<int>>& population
) {
    int pop_size = static_cast<int>(population.size());
    std::vector<std::array<float, 2>> objectives(pop_size);

    for (int ind = 0; ind < pop_size; ++ind) {
        const auto& chrom = population[ind];

        // Team ratings
        std::vector<float> team_ratings(num_teams_, 0.0f);
        float role_std_sum = 0.0f;

        for (int t = 0; t < num_teams_; ++t) {
            std::vector<float> role_ratings(num_roles_, 0.0f);
            std::vector<int> role_counts(num_roles_, 0);

            for (int s = 0; s < players_in_team_; ++s) {
                int slot_idx = t * players_in_team_ + s;
                int p_idx = chrom[slot_idx];
                int r_idx = team_slots_[s];
                team_ratings[t] += static_cast<float>(R_[p_idx][r_idx]);
                role_ratings[r_idx] += static_cast<float>(R_[p_idx][r_idx]);
                role_counts[r_idx]++;
            }

            for (int r = 0; r < num_roles_; ++r) {
                if (role_counts[r] > 1) {
                    float mean = role_ratings[r] / static_cast<float>(role_counts[r]);
                    float variance = 0.0f;
                    // We need per-player ratings for this role within the team
                    // But since we only have aggregated, approximate with 0 for same-role players
                    // Actually the Python version uses R_vals.std(axis=1) which is std across players
                    // for each role slot. Let's compute it properly.
                }
            }
        }

        // Recompute role_std properly: for each team, for each slot position,
        // get the rating, then compute std across slots grouped by role
        // The Python version: R_vals has shape (Pop, Teams, PlayersInTeam)
        // R_vals.std(axis=1) gives std across teams for each slot position
        // Then .sum(axis=1) sums across slot positions
        // This means: for each slot position j, compute std of R[chrom[t*players_in_team+j]][team_slots[j]] across all teams t

        role_std_sum = 0.0f;
        for (int s = 0; s < players_in_team_; ++s) {
            float sum = 0.0f;
            for (int t = 0; t < num_teams_; ++t) {
                int slot_idx = t * players_in_team_ + s;
                int p_idx = chrom[slot_idx];
                int r_idx = team_slots_[s];
                sum += static_cast<float>(R_[p_idx][r_idx]);
            }
            float mean = sum / static_cast<float>(num_teams_);
            float variance = 0.0f;
            for (int t = 0; t < num_teams_; ++t) {
                int slot_idx = t * players_in_team_ + s;
                int p_idx = chrom[slot_idx];
                int r_idx = team_slots_[s];
                float diff = static_cast<float>(R_[p_idx][r_idx]) - mean;
                variance += diff * diff;
            }
            variance /= static_cast<float>(num_teams_);
            role_std_sum += std::sqrt(variance);
        }

        float team_max = team_ratings[0];
        float team_min = team_ratings[0];
        float team_mean = 0.0f;
        for (float r : team_ratings) {
            if (r > team_max) team_max = r;
            if (r < team_min) team_min = r;
            team_mean += r;
        }
        team_mean /= static_cast<float>(num_teams_);

        float team_variance_sum = 0.0f;
        for (float r : team_ratings) {
            float diff = r - team_mean;
            team_variance_sum += diff * diff;
        }
        float team_std = std::sqrt(team_variance_sum / static_cast<float>(num_teams_));

        objectives[ind][0] = nsga_settings_.weight_team_variance * ((team_max - team_min) + team_std)
                           + nsga_settings_.weight_role_variance * role_std_sum;

        // Priority penalties
        float priority_penalty = 0.0f;
        for (int t = 0; t < num_teams_; ++t) {
            for (int s = 0; s < players_in_team_; ++s) {
                int slot_idx = t * players_in_team_ + s;
                int p_idx = chrom[slot_idx];
                int r_idx = team_slots_[s];
                int prio = P_[p_idx][r_idx];
                int prio_clamped = std::max(0, std::min(3, prio));
                priority_penalty += priority_penalties_[prio_clamped];
            }
        }
        objectives[ind][1] = priority_penalty;
    }

    return objectives;
}

std::vector<std::vector<int>> NSGA2Engine::fast_non_dominated_sort(
    const std::vector<std::array<float, 2>>& objectives
) {
    int pop_size = static_cast<int>(objectives.size());
    std::vector<std::vector<int>> S(pop_size);
    std::vector<int> n(pop_size, 0);
    std::vector<int> rank(pop_size, 0);
    std::vector<std::vector<int>> fronts;

    for (int p = 0; p < pop_size; ++p) {
        for (int q = 0; q < pop_size; ++q) {
            if (p == q) continue;
            bool p_dominates_q = (objectives[p][0] <= objectives[q][0] && objectives[p][1] <= objectives[q][1])
                              && (objectives[p][0] < objectives[q][0] || objectives[p][1] < objectives[q][1]);
            bool q_dominates_p = (objectives[q][0] <= objectives[p][0] && objectives[q][1] <= objectives[p][1])
                              && (objectives[q][0] < objectives[p][0] || objectives[q][1] < objectives[p][1]);

            if (p_dominates_q) {
                S[p].push_back(q);
            }
            if (q_dominates_p) {
                n[p]++;
            }
        }
        if (n[p] == 0) {
            rank[p] = 0;
            if (fronts.empty()) fronts.emplace_back();
            fronts[0].push_back(p);
        }
    }

    int i = 0;
    while (i < static_cast<int>(fronts.size()) && !fronts[i].empty()) {
        std::vector<int> next_front;
        for (int p : fronts[i]) {
            for (int q : S[p]) {
                n[q]--;
                if (n[q] == 0) {
                    rank[q] = i + 1;
                    next_front.push_back(q);
                }
            }
        }
        if (!next_front.empty()) {
            fronts.push_back(std::move(next_front));
        }
        i++;
    }

    if (!fronts.empty() && fronts.back().empty()) {
        fronts.pop_back();
    }

    return fronts;
}

std::vector<float> NSGA2Engine::calculate_crowding_distance(
    const std::vector<std::array<float, 2>>& objectives,
    const std::vector<std::vector<int>>& fronts
) {
    int pop_size = static_cast<int>(objectives.size());
    int num_obj = 2;
    std::vector<float> distances(pop_size, 0.0f);

    for (const auto& front : fronts) {
        if (front.size() <= 2) {
            for (int idx : front) {
                distances[idx] = std::numeric_limits<float>::infinity();
            }
            continue;
        }

        for (int m = 0; m < num_obj; ++m) {
            std::vector<int> sorted_front = front;
            std::sort(sorted_front.begin(), sorted_front.end(),
                [&objectives, m](int a, int b) {
                    return objectives[a][m] < objectives[b][m];
                });

            distances[sorted_front.front()] = std::numeric_limits<float>::infinity();
            distances[sorted_front.back()] = std::numeric_limits<float>::infinity();

            float f_min = objectives[sorted_front.front()][m];
            float f_max = objectives[sorted_front.back()][m];
            float range = f_max - f_min;

            if (range == 0.0f) continue;

            for (size_t k = 1; k + 1 < sorted_front.size(); ++k) {
                distances[sorted_front[k]] +=
                    (objectives[sorted_front[k + 1]][m] - objectives[sorted_front[k - 1]][m]) / range;
            }
        }
    }

    return distances;
}

std::vector<int> NSGA2Engine::tournament_selection(
    int num_select,
    const std::vector<int>& ranks,
    const std::vector<float>& distances,
    const std::vector<std::array<float, 2>>& objectives
) {
    std::vector<int> selected;
    selected.reserve(num_select);

    std::uniform_int_distribution<int> dist(0, static_cast<int>(ranks.size()) - 1);

    for (int i = 0; i < num_select; ++i) {
        int idx1 = dist(rng_);
        int idx2 = dist(rng_);
        while (idx2 == idx1) idx2 = dist(rng_);

        if (ranks[idx1] < ranks[idx2]) {
            selected.push_back(idx1);
        } else if (ranks[idx1] > ranks[idx2]) {
            selected.push_back(idx2);
        } else {
            selected.push_back(distances[idx1] > distances[idx2] ? idx1 : idx2);
        }
    }

    return selected;
}

std::vector<int> NSGA2Engine::mutate(const std::vector<int>& parent) {
    std::vector<int> chrom = parent;

    std::uniform_real_distribution<float> prob_dist(0.0f, 1.0f);
    float mutation_type = prob_dist(rng_);

    if (mutation_type < 0.35f) {
        // Fix invalid roles
        std::vector<int> invalid_indices;
        for (int i = 0; i < num_players_; ++i) {
            int slot_idx = i % players_in_team_;
            int role_idx = team_slots_[slot_idx];
            if (P_[chrom[i]][role_idx] == 0) {
                invalid_indices.push_back(i);
            }
        }

        if (!invalid_indices.empty()) {
            std::uniform_int_distribution<int> inv_dist(0, static_cast<int>(invalid_indices.size()) - 1);
            int idx1 = invalid_indices[inv_dist(rng_)];
            int r_idx = team_slots_[idx1 % players_in_team_];

            std::vector<int> valid_candidates;
            for (int i = 0; i < num_players_; ++i) {
                if (team_slots_[i % players_in_team_] == r_idx && i != idx1) {
                    valid_candidates.push_back(i);
                }
            }

            if (!valid_candidates.empty()) {
                std::uniform_int_distribution<int> cand_dist(0, static_cast<int>(valid_candidates.size()) - 1);
                int idx2 = valid_candidates[cand_dist(rng_)];
                std::swap(chrom[idx1], chrom[idx2]);
                return chrom;
            }
        }
    }
    else if (mutation_type < 0.70f) {
        // Robin Hood mutation
        std::vector<float> team_ratings(num_teams_, 0.0f);
        for (int t = 0; t < num_teams_; ++t) {
            for (int s = 0; s < players_in_team_; ++s) {
                int slot_idx = t * players_in_team_ + s;
                int p_idx = chrom[slot_idx];
                int r_idx = team_slots_[s];
                team_ratings[t] += static_cast<float>(R_[p_idx][r_idx]);
            }
        }

        int best_team = 0;
        int worst_team = 0;
        for (int t = 1; t < num_teams_; ++t) {
            if (team_ratings[t] > team_ratings[best_team]) best_team = t;
            if (team_ratings[t] < team_ratings[worst_team]) worst_team = t;
        }

        std::uniform_int_distribution<int> slot_dist(0, players_in_team_ - 1);
        int s = slot_dist(rng_);
        int idx1 = best_team * players_in_team_ + s;
        int idx2 = worst_team * players_in_team_ + s;
        std::swap(chrom[idx1], chrom[idx2]);
        return chrom;
    }
    else if (mutation_type < 0.90f) {
        // Priority improvement
        std::uniform_int_distribution<int> team_dist(0, num_teams_ - 1);
        int t1 = team_dist(rng_);
        int t2 = team_dist(rng_);
        while (t2 == t1) t2 = team_dist(rng_);

        std::uniform_int_distribution<int> slot_dist(0, players_in_team_ - 1);
        int s1 = slot_dist(rng_);
        int s2 = slot_dist(rng_);
        while (s2 == s1) s2 = slot_dist(rng_);

        int idx1 = t1 * players_in_team_ + s1;
        int idx2 = t2 * players_in_team_ + s2;

        int p1 = chrom[idx1];
        int p2 = chrom[idx2];
        int r1 = team_slots_[s1];
        int r2 = team_slots_[s2];

        int cur_prio = 0;
        int prio1 = std::max(0, std::min(3, P_[p1][r1]));
        int prio2 = std::max(0, std::min(3, P_[p2][r2]));
        cur_prio = static_cast<int>(priority_penalties_[prio1]) + static_cast<int>(priority_penalties_[prio2]);

        int new_prio = 0;
        int new_prio1 = std::max(0, std::min(3, P_[p1][r2]));
        int new_prio2 = std::max(0, std::min(3, P_[p2][r1]));
        new_prio = static_cast<int>(priority_penalties_[new_prio1]) + static_cast<int>(priority_penalties_[new_prio2]);

        if (new_prio <= cur_prio) {
            std::swap(chrom[idx1], chrom[idx2]);
        }
        return chrom;
    }

    // Random shake
    std::uniform_int_distribution<int> dist(0, num_players_ - 1);
    int idx1 = dist(rng_);
    int idx2 = dist(rng_);
    while (idx2 == idx1) idx2 = dist(rng_);
    std::swap(chrom[idx1], chrom[idx2]);
    return chrom;
}

std::vector<DraftSolution> NSGA2Engine::decode_results(
    const std::vector<std::vector<int>>& chroms,
    const std::vector<std::array<float, 2>>& objs
) {
    std::vector<DraftSolution> solutions;
    solutions.reserve(chroms.size());

    for (size_t sol_id = 0; sol_id < chroms.size(); ++sol_id) {
        const auto& chrom = chroms[sol_id];
        DraftSolution sol;
        sol.solution_id = static_cast<int>(sol_id) + 1;
        sol.fitness_balance = objs[sol_id][0];
        sol.fitness_priority = objs[sol_id][1];

        sol.teams.resize(num_teams_);

        for (int t = 0; t < num_teams_; ++t) {
            TeamResult& team = sol.teams[t];
            team.team_id = t + 1;
            team.total_rating = 0;

            for (int s = 0; s < players_in_team_; ++s) {
                int slot_idx = t * players_in_team_ + s;
                int p_idx = chrom[slot_idx];
                int r_idx = team_slots_[s];

                AssignedPlayer ap;
                ap.member_id = p_idx;
                ap.role_id = role_ids_[r_idx];
                ap.rating = R_[p_idx][r_idx];
                ap.priority = P_[p_idx][r_idx];

                team.total_rating += ap.rating;
                team.players.push_back(std::move(ap));
            }
        }

        solutions.push_back(std::move(sol));
    }

    return solutions;
}

std::vector<DraftSolution> NSGA2Engine::run(const std::vector<PlayerInfo>& players) {
    build_matrices(players);

    int pop_size = nsga_settings_.population_size;

    // Initialize population
    std::vector<std::vector<int>> population(pop_size);
    for (int i = 0; i < pop_size; ++i) {
        population[i] = generate_individual();
    }

    auto objectives = evaluate_population(population);

    for (int gen = 0; gen < nsga_settings_.generations; ++gen) {
        auto fronts = fast_non_dominated_sort(objectives);

        std::vector<int> ranks(pop_size, 0);
        for (size_t r = 0; r < fronts.size(); ++r) {
            for (int idx : fronts[r]) {
                ranks[idx] = static_cast<int>(r);
            }
        }

        auto distances = calculate_crowding_distance(objectives, fronts);

        auto selected_indices = tournament_selection(pop_size, ranks, distances, objectives);

        std::vector<std::vector<int>> offspring(pop_size);
        for (int i = 0; i < pop_size; ++i) {
            offspring[i] = mutate(population[selected_indices[i]]);
        }

        auto offspring_objectives = evaluate_population(offspring);

        // Combined population
        std::vector<std::vector<int>> combined_pop(pop_size * 2);
        std::vector<std::array<float, 2>> combined_obj(pop_size * 2);

        for (int i = 0; i < pop_size; ++i) {
            combined_pop[i] = std::move(population[i]);
            combined_obj[i] = objectives[i];
        }
        for (int i = 0; i < pop_size; ++i) {
            combined_pop[pop_size + i] = std::move(offspring[i]);
            combined_obj[pop_size + i] = offspring_objectives[i];
        }

        auto fronts_comb = fast_non_dominated_sort(combined_obj);

        std::vector<int> next_pop_indices;
        next_pop_indices.reserve(pop_size);

        for (const auto& front : fronts_comb) {
            if (next_pop_indices.size() + front.size() <= static_cast<size_t>(pop_size)) {
                next_pop_indices.insert(next_pop_indices.end(), front.begin(), front.end());
            } else {
                auto distances_comb = calculate_crowding_distance(combined_obj, {front});
                std::vector<int> sorted_front = front;
                std::sort(sorted_front.begin(), sorted_front.end(),
                    [&distances_comb](int a, int b) {
                        return distances_comb[a] > distances_comb[b];
                    });

                size_t needed = pop_size - next_pop_indices.size();
                next_pop_indices.insert(next_pop_indices.end(), sorted_front.begin(), sorted_front.begin() + needed);
                break;
            }
        }

        // Build next generation
        population.resize(next_pop_indices.size());
        objectives.resize(next_pop_indices.size());
        for (size_t i = 0; i < next_pop_indices.size(); ++i) {
            population[i] = std::move(combined_pop[next_pop_indices[i]]);
            objectives[i] = combined_obj[next_pop_indices[i]];
        }
    }

    // Extract Pareto front
    auto final_fronts = fast_non_dominated_sort(objectives);
    if (final_fronts.empty() || final_fronts[0].empty()) {
        return {};
    }

    const auto& pareto_front = final_fronts[0];

    // Filter duplicates
    std::vector<int> unique_front;
    std::set<std::pair<float, float>> seen_fitness;

    for (int idx : pareto_front) {
        float fit_balance = std::round(objectives[idx][0] * 10000.0f) / 10000.0f;
        float fit_priority = std::round(objectives[idx][1] * 10000.0f) / 10000.0f;
        auto fit_tuple = std::make_pair(fit_balance, fit_priority);

        if (seen_fitness.find(fit_tuple) == seen_fitness.end()) {
            seen_fitness.insert(fit_tuple);
            unique_front.push_back(idx);
        }
    }

    // Sort by balance
    std::sort(unique_front.begin(), unique_front.end(),
        [&objectives](int a, int b) {
            return objectives[a][0] < objectives[b][0];
        });

    int num_to_select = std::min(nsga_settings_.num_pareto_solutions, static_cast<int>(unique_front.size()));
    if (num_to_select == 0) return {};

    int step = std::max(1, static_cast<int>(unique_front.size()) / num_to_select);

    std::vector<int> selected_solutions;
    selected_solutions.reserve(num_to_select);
    for (int i = 0; i < static_cast<int>(unique_front.size()) && static_cast<int>(selected_solutions.size()) < num_to_select; i += step) {
        selected_solutions.push_back(unique_front[i]);
    }

    // Build result chromosomes and objectives
    std::vector<std::vector<int>> result_chroms;
    std::vector<std::array<float, 2>> result_objs;
    result_chroms.reserve(selected_solutions.size());
    result_objs.reserve(selected_solutions.size());

    for (int idx : selected_solutions) {
        result_chroms.push_back(population[idx]);
        result_objs.push_back(objectives[idx]);
    }

    return decode_results(result_chroms, result_objs);
}
