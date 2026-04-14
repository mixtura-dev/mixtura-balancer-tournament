import datetime
from uuid import UUID

from pydantic import BaseModel


class QualityMetrics(BaseModel):
    dp_fairness: float = 0.0
    dp_role_fairness: float = 0.0
    vq_uniformity: float = 0.0
    role_priority_points: float = 0.0
    fitness_balance: float = 0.0
    fitness_priority: float = 0.0
    fitness_subrole: float = 0.0
    role_subrole_penalty: float = 0.0
    evaluation: float = 0.0

    @property
    def uniformity(self) -> float:
        return self.vq_uniformity

    @property
    def fairness(self) -> float:
        return self.dp_fairness

    @property
    def role_points(self) -> float:
        return self.role_priority_points

    @property
    def role_fairness(self) -> float:
        return self.dp_role_fairness


class TeamPlayer(BaseModel):
    member_id: UUID
    game_role_id: UUID
    rating: int
    priority: int = 0


class Team(BaseModel):
    id: UUID
    players: list[TeamPlayer]
    total_rating: int = 0


class Balance(BaseModel):
    id: UUID
    quality: QualityMetrics
    teams: list[Team]

    @property
    def fitness_balance(self) -> float:
        return self.quality.fitness_balance

    @property
    def fitness_priority(self) -> float:
        return self.quality.fitness_priority


class DraftBalances(BaseModel):
    draft_id: UUID
    balances: list[Balance]
    created_at: datetime.datetime
