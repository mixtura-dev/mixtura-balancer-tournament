"""
Test client for RabbitMQ balance service.
"""
import asyncio
import uuid
from asyncio import Future
from datetime import datetime, timezone
from typing import Annotated

from faststream import Context, FastStream
from faststream.rabbit import RabbitBroker

from mixtura_balancer_tournament.app.schemas import ResponseMessage
from mixtura_balancer_tournament.domain.models.balance import DraftBalances
from mixtura_balancer_tournament.domain.models.balance_request import (
    BalanceRequest,
    BalanceSettings,
    MathSettings,
    Player,
    PlayerRole,
    RoleSettings,
)
from mixtura_balancer_tournament.env_config import env


def create_test_request(num_teams: int = 4, players_per_team: int = 5) -> BalanceRequest:
    role_tank = uuid.UUID("11111111-1111-1111-1111-111111111111")
    role_dps = uuid.UUID("22222222-2222-2222-2222-222222222222")
    role_support = uuid.UUID("33333333-3333-3333-3333-333333333333")

    roles_settings = {
        role_tank: RoleSettings(original_game_role=role_tank, count_in_team=1),
        role_dps: RoleSettings(original_game_role=role_dps, count_in_team=2),
        role_support: RoleSettings(original_game_role=role_support, count_in_team=2),
    }

    players_in_team = players_per_team
    total_players = num_teams * players_in_team

    players = []
    for i in range(total_players):
        player_id = uuid.uuid4()
        base_rating = 1200 + (i % 10) * 100

        roles = {
            role_tank: PlayerRole(priority=i % 3 + 1, rating=base_rating),
            role_dps: PlayerRole(priority=(i + 1) % 3 + 1, rating=base_rating + 50),
            role_support: PlayerRole(priority=(i + 2) % 3 + 1, rating=base_rating + 25),
        }
        players.append(Player(member_id=player_id, roles=roles))

    math_settings = MathSettings(
        population_size=100,
        generations=50,
        num_pareto_solutions=10,
    )

    balance_settings = BalanceSettings(
        players_in_team=players_in_team,
        roles=roles_settings,
        math=math_settings,
    )

    return BalanceRequest(
        draft_id=uuid.uuid4(),
        players=players,
        balance_settings=balance_settings,
    )


async def send_request(broker: RabbitBroker, request: BalanceRequest) -> DraftBalances:
    response = await broker.request(
        message=request,
        queue="mix_balance_service.balance",
        timeout=60,
    )
    response = ResponseMessage[DraftBalances].model_validate_json(response.body)
    print(f"Received response with status={response.status}")
    return response.message


async def main():
    request = create_test_request(num_teams=4, players_per_team=5)

    print(f"Request: {len(request.players)} players, {request.balance_settings.players_in_team} per team")
    print(f"Draft ID: {request.draft_id}")

    async with RabbitBroker(env.rabbit.url) as broker:
        response = await send_request(broker, request)

    print(f"Got {len(response.balances)} balance solutions")
    if response.balances:
        first = response.balances[0]
        print(f"First balance:")
        print(f"  Teams: {len(first.teams)}")
        for i, team in enumerate(first.teams):
            print(f"    Team {i+1}: {len(team.players)} players, total_rating={team.total_rating}")
        print(f"  Quality: dp_fairness={first.quality.dp_fairness:.3f}, "
              f"dp_role_fairness={first.quality.dp_role_fairness:.3f}")


if __name__ == "__main__":
    asyncio.run(main())