use base64;
use rand::{Rng, thread_rng};

pub fn elephant() -> Option<String> {
    let mascots = [include_str!("elephants/big"), include_str!("elephants/small")];

    thread_rng()
        .choose(&mascots)
        .and_then(|choice| base64::decode(choice).ok())
        .and_then(|raw| String::from_utf8(raw).ok())
}
