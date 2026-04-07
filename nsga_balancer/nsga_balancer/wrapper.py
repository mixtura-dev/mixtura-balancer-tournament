# wrapper.py
"""
NSGA Balancer wrapper for C++ bindings.
"""

from __future__ import annotations

import uuid
from concurrent.futures import ThreadPoolExecutor
from typing import Any

from . import _core
from .models import (
    AssignedPlayer,
    BalanceRequest,
    DraftSolution,
    MathSettings,
    RoleSettings,
    Team,
)


class UUIDMapper:
    __slots__ = ("_uuid_to_int", "_int_to_uuid", "_next_id")

    def __init__(self) -> None:
        self._uuid_to_int: dict[uuid.UUID, int] = {}
        self._int_to_uuid: dict[int, uuid.UUID] = {}
        self._next_id: int = 0

    def register(self, uid: uuid.UUID) -> int:
        if uid in self._uuid_to_int:
            return self._uuid_to_int[uid]
        int_id = self._next_id
        self._uuid_to_int[uid] = int_id
        self._int_to_uuid[int_id] = uid
        self._next_id += 1
        return int_id

    def to_int(self, uid: uuid.UUID) -> int:
        return self._uuid_to_int[uid]

    def to_uuid(self, int_id: int) -> uuid.UUID:
        return self._int_to_uuid[int_id]

    def register_all(self, uids: list[uuid.UUID]) -> list[int]:
        return [self.register(uid) for uid in uids]


class NSGA2Balancer:
    def __init__(self, request: BalanceRequest) -> None:
        self.request = request
        self.settings = request.balance_settings.math
        self.players_in_team = request.balance_settings.players_in_team

        self._role_mapper = UUIDMapper()
        self._player_uuids = [p.member_id for p in request.players]
        self._role_uuids = list(request.balance_settings.roles.keys())
        self._cpp_role_ids = self._role_mapper.register_all(self._role_uuids)

        cpp_nsga_settings = self._convert_nsga_settings(self.settings)
        cpp_engine_settings = _core.create_engine_settings(0, 4, 42)
        cpp_role_settings = self._convert_role_settings(request.balance_settings.roles)

        self._cpp_engine = _core.NSGA2Engine(
            cpp_nsga_settings,
            self._cpp_role_ids,
            cpp_role_settings,
            self.players_in_team,
            cpp_engine_settings,
        )

    def _convert_nsga_settings(self, settings: MathSettings) -> _core.NSGASettings:
        cpp_settings = _core.NSGASettings()
        cpp_settings.population_size = settings.population_size
        cpp_settings.generations = settings.generations
        cpp_settings.num_pareto_solutions = settings.num_pareto_solutions
        cpp_settings.weight_team_variance = settings.weight_team_variance
        cpp_settings.weight_role_variance = settings.weight_role_variance
        cpp_settings.penalty_invalid_role = settings.penalty_invalid_role
        cpp_settings.penalty_prio_1 = settings.penalty_prio_1
        cpp_settings.penalty_prio_2 = settings.penalty_prio_2
        cpp_settings.penalty_prio_3 = settings.penalty_prio_3
        return cpp_settings

    def _convert_role_settings(
        self, roles: dict[uuid.UUID, RoleSettings]
    ) -> dict[int, _core.RoleSettings]:
        return {
            self._role_mapper.to_int(role_uuid): _core.create_role_settings(
                role_settings.count_in_team
            )
            for role_uuid, role_settings in roles.items()
        }

    def _convert_players(
        self, players: list, member_mapper: UUIDMapper
    ) -> list[_core.PlayerInfo]:
        cpp_players = []
        for player in players:
            member_int_id = member_mapper.register(player.member_id)
            cpp_roles = []
            for role_uuid, role_info in player.roles.items():
                role_int_id = self._role_mapper.to_int(role_uuid)
                cpp_roles.append((role_int_id, role_info.rating, role_info.priority))
            cpp_players.append(_core.create_player(member_int_id, cpp_roles))
        return cpp_players

    def _convert_results(
        self, cpp_solutions: list, member_mapper: UUIDMapper
    ) -> list[DraftSolution]:
        solutions = []
        for cpp_sol in cpp_solutions:
            teams = []
            for cpp_team in cpp_sol.teams:
                team_players = []
                for cpp_player in cpp_team.players:
                    team_players.append(
                        AssignedPlayer(
                            member_id=member_mapper.to_uuid(cpp_player.member_id),
                            role_id=self._role_mapper.to_uuid(cpp_player.role_id),
                            rating=cpp_player.rating,
                            priority=cpp_player.priority,
                        )
                    )
                teams.append(
                    Team(
                        team_id=cpp_team.team_id,
                        players=team_players,
                        total_rating=cpp_team.total_rating,
                    )
                )
            solutions.append(
                DraftSolution(
                    solution_id=cpp_sol.solution_id,
                    fitness_balance=cpp_sol.fitness_balance,
                    fitness_priority=cpp_sol.fitness_priority,
                    teams=teams,
                )
            )
        return solutions

    def run(self) -> list[DraftSolution]:
        member_mapper = UUIDMapper()
        cpp_players = self._convert_players(self.request.players, member_mapper)
        cpp_solutions = self._cpp_engine.run(cpp_players)
        return self._convert_results(cpp_solutions, member_mapper)


def balance_teams_nsga(request: BalanceRequest) -> list[DraftSolution]:
    balancer = NSGA2Balancer(request)
    return balancer.run()


__all__ = [
    "NSGA2Balancer",
    "UUIDMapper",
    "balance_teams_nsga",
]
