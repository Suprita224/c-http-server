import socket
from concurrent.futures import ThreadPoolExecutor

HOST, PORT = "localhost", 8080


def raw_request(text):
    """Send raw text to the server and return the full reply."""
    with socket.create_connection((HOST, PORT), timeout=5) as s:
        s.sendall(text.encode())
        data = b""
        while True:
            chunk = s.recv(4096)
            if not chunk:
                break
            data += chunk
    return data.decode(errors="ignore")


def first_line(reply):
    return reply.split("\r\n")[0]


def get(path):
    return raw_request(f"GET {path} HTTP/1.1\r\nHost: x\r\n\r\n")


def test_home_page():
    assert "200" in first_line(get("/"))


def test_missing_file():
    assert "404" in first_line(get("/missing.html"))


def test_path_traversal_blocked():
    assert "403" in first_line(get("/../main.c"))


def test_post_not_allowed():
    reply = raw_request("POST / HTTP/1.1\r\nHost: x\r\n\r\n")
    assert "405" in first_line(reply)


def test_css_content_type():
    assert "Content-Type: text/css" in get("/style.css")


def test_many_visitors_at_once():
    with ThreadPoolExecutor(50) as pool:
        results = list(pool.map(lambda _: first_line(get("/")), range(200)))
    assert all("200" in r for r in results)


if __name__ == "__main__":
    for name, fn in list(globals().items()):
        if name.startswith("test_"):
            fn()
            print("PASS", name)
    print("All tests passed")
