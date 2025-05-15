use serde::{Deserialize, Serialize};

pub type UUIDv7 = [u8; 32];

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct Login {
    pub username: String,
    pub password: String,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct ChatJoin {
    pub chat_id: UUIDv7,
    pub user_id: UUIDv7,
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
pub struct MsgSend {
    pub chat_id: UUIDv7,
    pub user_id: UUIDv7,
    pub msg: String
}

#[derive(Debug, Clone, Serialize, Deserialize, PartialEq, Eq)]
#[serde(tag = "action", rename_all = "snake_case")]
pub enum ClientMessage {
    Login(Login),
    ChatJoin(ChatJoin),
    MsgSend(MsgSend)
}
