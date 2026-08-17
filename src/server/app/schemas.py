from typing import Literal, Optional

from pydantic import BaseModel, Field

UpdateMode = Literal["manual", "automatic"]
JobStatus = Literal[
    "pending", "downloading", "verified", "installing",
    "success", "failed", "rolled_back",
]


class RegisterIn(BaseModel):
    device_uid: str = Field(min_length=1, max_length=64)
    current_version: str = Field(min_length=1, max_length=32)
    update_mode: Optional[UpdateMode] = None


class ModeIn(BaseModel):
    update_mode: UpdateMode


class ManualUpdateIn(BaseModel):
    firmware_id: int


class StatusIn(BaseModel):
    status: JobStatus
    message: Optional[str] = Field(default=None, max_length=512)
