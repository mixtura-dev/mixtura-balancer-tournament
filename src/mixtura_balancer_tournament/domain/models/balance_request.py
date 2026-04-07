from uuid import UUID

from pydantic import BaseModel, Field, model_validator


class PlayerRole(BaseModel):
    priority: int = Field(ge=0)
    rating: int = Field(ge=0)


class Player(BaseModel):
    member_id: UUID
    roles: dict[UUID, PlayerRole]


class RoleSettings(BaseModel):
    original_game_role: UUID = Field(description="Оригинальная роль в игре")
    count_in_team: int = Field(ge=0, description="Количество игроков этой роли в команде")

    @property
    def min_in_team(self) -> int:
        return self.count_in_team

    @property
    def max_in_team(self) -> int:
        return self.count_in_team

    @model_validator(mode="before")
    @classmethod
    def handle_legacy_fields(cls, data):
        if isinstance(data, dict):
            if "max_in_team" in data and "count_in_team" not in data:
                data["count_in_team"] = data.pop("max_in_team")
            if "min_in_team" in data:
                data.pop("min_in_team", None)
        return data


class MathSettings(BaseModel):
    fairness_coef: float = Field(default=3.0, description="Вес fairness")
    role_fairness_coef: float = Field(default=1.0, description="Вес role fairness")
    role_priority_coef: float = Field(default=80.0, description="Вес role priority")
    role_priority_imbalance_coef: float = Field(
        default=0.2, ge=0, description="Вес штрафа за дисбаланс приоритетов"
    )
    fairness_power_coef: float = Field(default=2.0, ge=1, description="Степень для fairness")
    uniformity_power_coef: float = Field(default=2.0, ge=1, description="Степень для uniformity")
    role_priority_imbalance_threshold: int = Field(default=1, ge=0, description="Порог дисбаланса приоритетов")
    
    population_size: int = Field(default=200, ge=1, description="Размер популяции NSGA-II")
    generations: int = Field(default=1000, ge=1, description="Количество поколений NSGA-II")
    num_pareto_solutions: int = Field(default=50, ge=1, description="Количество решений из Парето-фронта")
    weight_team_variance: float = Field(default=1.0, description="Вес дисперсии команд")
    weight_role_variance: float = Field(default=0.5, description="Вес дисперсии ролей")
    penalty_invalid_role: float = Field(default=10000.0, description="Штраф за невалидную роль")
    penalty_prio_1: float = Field(default=10.0, description="Штраф за приоритет 1")
    penalty_prio_2: float = Field(default=3.0, description="Штраф за приоритет 2")
    penalty_prio_3: float = Field(default=0.0, description="Штраф за приоритет 3")


class BalanceSettings(BaseModel):
    players_in_team: int = Field(ge=1, description="Количество игроков в команде")
    roles: dict[UUID, RoleSettings] = Field(
        default={}, description="Настойки ролей: UUID роли -> настройки"
    )
    math: MathSettings = Field(default_factory=MathSettings, description="Математические настройки")

    @property
    def max_in_team(self) -> int:
        return self.players_in_team

    @model_validator(mode="before")
    @classmethod
    def handle_legacy_fields(cls, data):
        if isinstance(data, dict):
            if "max_in_team" in data and "players_in_team" not in data:
                data["players_in_team"] = data.pop("max_in_team")
        return data

    @model_validator(mode="after")
    def validate_settings(self) -> "BalanceSettings":
        total_count = sum(r.count_in_team for r in self.roles.values())
        if total_count != self.players_in_team:
            raise ValueError(
                f"Sum of role counts ({total_count}) must equal players_in_team ({self.players_in_team})"
            )
        return self


class BalanceRequest(BaseModel):
    draft_id: UUID
    players: list[Player]
    balance_settings: BalanceSettings

    @model_validator(mode="after")
    def validate_players_roles(self) -> "BalanceRequest":
        role_ids = set(self.balance_settings.roles.keys())
        for player in self.players:
            for role_id in player.roles.keys():
                if role_id not in role_ids:
                    raise ValueError(f"Player {player.member_id} has undefined role {role_id}")
        return self

    @model_validator(mode="after")
    def validate_players_count(self) -> "BalanceRequest":
        if len(self.players) % self.balance_settings.players_in_team != 0:
            raise ValueError(
                f"Number of players ({len(self.players)}) must be divisible by "
                f"players_in_team ({self.balance_settings.players_in_team})"
            )
        return self
