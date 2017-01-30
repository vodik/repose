use nom::{IResult, space, alpha, alphanumeric, digit};

named!(name_parser<&str>,
    chain!(
        tag!("hello") ~
        space? ~
        name: map_res!(
            alpha,
            std::str::from_utf8
        ) ,

        || name
    )
);

#[test]
fn test_name_parser() {
    let empty = &b""[..];
    assert_eq!(name_parser("hello Herman".as_bytes()), IResult::Done(empty, ("Herman")));
    assert_eq!(name_parser("hello Kimberly".as_bytes()), IResult::Done(empty, ("Kimberly")));
}
