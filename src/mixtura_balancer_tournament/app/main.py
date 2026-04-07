import logging

from faststream import Depends, ExceptionMiddleware, FastStream
from faststream.rabbit import Channel, RabbitBroker, RabbitRouter

from mixtura_balancer_tournament.env_config import env


from ..domain.balance_engine import get_engine
from ..domain.models.balance import DraftBalances
from ..domain.models.balance_request import BalanceRequest
from ..logging_setup import setup_logging
from .exceptions import DomainException
from .schemas import ErrorResponse, ResponseMessage

exc_middleware = ExceptionMiddleware()
logger = logging.getLogger(__name__)


@exc_middleware.add_handler(DomainException, publish=True)
async def error_handler(exc: DomainException) -> ResponseMessage[ErrorResponse]:
    return ResponseMessage(status=exc.status_code, message=ErrorResponse(message=exc.message))


broker = RabbitBroker(
    env.rabbit.url,
    middlewares=[exc_middleware],
    default_channel=Channel(prefetch_count=10),
)

router = RabbitRouter()


@router.subscriber("mix_balance_service.balance")
async def balance_handler(
    message: BalanceRequest
) -> ResponseMessage[DraftBalances | ErrorResponse]:
    logger.info(
        f"Received balance request for draft_id={message.draft_id} with {len(message.players)} players"
    )
    engine = get_engine()
    result = await engine.find_balances_async(message)
    return ResponseMessage(status=200, message=result)


broker.include_router(router)

app = FastStream(broker)


@app.on_startup
async def startup():
    setup_logging()
