"""The upload of a packaged page to a site: `PUT https://<site>/api/v1/builds/<page>` with the package as one plain tar (the contract is the
site's `docs/builds.md`). `build_and_deploy.py` calls it for a target that names a `site` and a `page`.

    read_token(env_dir)                       the bearer token, from ENGINE_SITE_UPLOAD_TOKEN of the environment or of `<env_dir>/.env`
    build_tar(dist, tar_path) -> int          the files of the package folder at the root of an uncompressed tar; returns their count
    file_sha256(path) -> str
    upload(site, page, tar_path, sha256, token) -> dict    the site's answer on 200; UploadError on anything else

The token is a parameter of `upload` and goes into one header, never into a message, a `repr` or a printed command: every error text is
built here from fixed words, the status, the site's one-line `detail` (cleaned, with the token cut out of it) and the type and `strerror`
of a socket error. Standard library only.
"""

import hashlib
import http.client
import json
import os
import re
import tarfile
from pathlib import Path

ENV_VARIABLE = "ENGINE_SITE_UPLOAD_TOKEN"
ENV_FILE = ".env"
# Printable ASCII with no space: whatever the value holds ends up in one header line, so nothing that could end the line may pass
TOKEN_PATTERN = re.compile(r"[!-~]{1,512}")
PAGE_PATTERN = re.compile(r"[a-z0-9][a-z0-9-]*")
SHA256_PATTERN = re.compile(r"[0-9a-f]{64}")
BUILDS_PATH = "/api/v1/builds/"
# A timeout of each socket operation, not of the whole upload: ~180 MB on a slow line must not be cut while it moves, and the site itself
# gives up on a body that stops for 60 s (408), so a wait of this length after the last byte is the site's validation and switch
UPLOAD_TIMEOUT_SECONDS = 120
PIECE_BYTES = 1024 * 1024
# The answers of the site are short JSON; a longer body is not read to the end
MAX_ANSWER_BYTES = 64 * 1024
MAX_DETAIL_CHARS = 200
NO_DETAIL = "no detail"
# Files of the macOS tools that a package never lists and the site would refuse (422)
SKIPPED_NAMES = (".DS_Store",)
SKIPPED_PREFIX = "._"
HIDDEN_TOKEN = "***"


class UploadError(Exception):
    """The upload cannot be made or the site refused it; the message holds no token."""


def _clean_line(text: str, token: str | None = None) -> str:
    """One line of printable characters; the token, when the text happens to hold it, is cut out."""
    if token:
        text = text.replace(token, HIDDEN_TOKEN)
    return " ".join("".join(char if char.isprintable() else " " for char in text).split())


def _env_value(path: Path) -> str | None:
    """The value of ENV_VARIABLE in the `.env` file (the last one when it is repeated); None when the name is absent."""
    try:
        text = path.read_text(encoding="utf-8-sig")
    except UnicodeDecodeError:
        raise UploadError(f"{path} is not UTF-8 text, so {ENV_VARIABLE} cannot be read from it") from None
    except OSError as error:
        raise UploadError(f"cannot read {path}: {type(error).__name__}") from None
    value = None
    for number, line in enumerate(text.splitlines(), 1):
        line = line.strip()
        if line.startswith("export "):
            line = line[len("export "):].lstrip()
        name, separator, raw = line.partition("=")
        if not separator or name.strip() != ENV_VARIABLE:
            continue
        raw = raw.strip()
        if raw[:1] in ("'", '"'):
            closing = raw.find(raw[0], 1)
            rest = raw[closing + 1:].strip() if closing > 0 else "-"
            if closing < 0 or (rest and not rest.startswith("#")):
                raise UploadError(f"line {number} of {path} is malformed: the quote of {ENV_VARIABLE} does not close cleanly")
            value = raw[1:closing]
        else:
            value = re.split(r"\s#", raw, maxsplit=1)[0].strip()
    return value


def read_token(env_dir: Path, environ=os.environ) -> str:
    """The upload token: the process environment first, else `<env_dir>/.env`. The value must fit TOKEN_PATTERN (no space, no control
    character); a missing, empty or unusable one is an UploadError that names the variable and where it looked, never the value."""
    value, where = environ.get(ENV_VARIABLE), "the environment"
    env_file = Path(env_dir) / ENV_FILE
    if not value:
        if not env_file.is_file():
            raise UploadError(f"{ENV_VARIABLE} is not set in the environment and {env_file} does not exist")
        value, where = _env_value(env_file), str(env_file)
    if not value:
        raise UploadError(f"{ENV_VARIABLE} is missing or empty in the environment and in {env_file}")
    if not TOKEN_PATTERN.fullmatch(value):
        raise UploadError(f"{ENV_VARIABLE} in {where} is not usable: it must be printable ASCII with no space or control character")
    return value


def _tar_info(tar: tarfile.TarFile, path: Path, arcname: str) -> tarfile.TarInfo:
    info = tar.gettarinfo(str(path), arcname)
    info.uid = info.gid = 0
    info.uname = info.gname = ""
    return info


def _add_folder(tar: tarfile.TarFile, folder: Path, prefix: str) -> int:
    count = 0
    for path in sorted(folder.iterdir(), key=lambda entry: entry.name):
        if path.name in SKIPPED_NAMES or path.name.startswith(SKIPPED_PREFIX):
            continue
        arcname = prefix + path.name
        if path.is_symlink():
            raise UploadError(f"{path} is a symbolic link: a package holds only files and folders")
        if path.is_dir():
            tar.addfile(_tar_info(tar, path, arcname))
            count += _add_folder(tar, path, arcname + "/")
        elif path.is_file():
            with path.open("rb") as handle:
                tar.addfile(_tar_info(tar, path, arcname), handle)
            count += 1
        else:
            raise UploadError(f"{path} is neither a file nor a folder: a package holds only those")
    return count


def build_tar(dist: Path, tar_path: Path) -> int:
    """Writes the package folder `dist` as an uncompressed tar: paths relative to `dist` with `/`, sorted, owner 0, no `.DS_Store` and
    no `._*` files. `tarfile`, not the `tar` of macOS, which adds the `._*` attribute files the manifest does not list. USTAR keeps the
    archive free of extension records. Returns the number of files."""
    dist = Path(dist)
    if not dist.is_dir():
        raise UploadError(f"{dist} is not a folder: nothing to upload")
    try:
        with tarfile.open(tar_path, "w", format=tarfile.USTAR_FORMAT) as tar:
            return _add_folder(tar, dist, "")
    except ValueError as error:
        # USTAR refuses a name that does not fit its fields
        raise UploadError(f"cannot write the tar of {dist}: {error}") from None
    except OSError as error:
        raise UploadError(f"cannot write the tar of {dist}: {_error_text(error)}") from None


def file_sha256(path: Path) -> str:
    digest = hashlib.sha256()
    with Path(path).open("rb") as handle:
        while piece := handle.read(PIECE_BYTES):
            digest.update(piece)
    return digest.hexdigest()


def _error_text(error: BaseException) -> str:
    """An OS, socket or TLS error by its type and `strerror`, nothing else: an exception text may quote what was sent or received."""
    reason = getattr(error, "strerror", None) or getattr(error, "reason", None)
    return f"{type(error).__name__}: {_clean_line(str(reason))}" if reason else type(error).__name__


def _detail(body: bytes, token: str) -> str:
    try:
        detail = json.loads(body).get("detail")
    except (ValueError, AttributeError):
        return NO_DETAIL
    if not isinstance(detail, str):
        return NO_DETAIL
    detail = _clean_line(detail, token)
    if not detail:
        return NO_DETAIL
    return detail if len(detail) <= MAX_DETAIL_CHARS else detail[:MAX_DETAIL_CHARS] + "..."


def _answer(connection: http.client.HTTPConnection, site: str, token: str, timeout: float) -> tuple[int, bytes]:
    """The status and the (limited) body of the site's answer."""
    try:
        response = connection.getresponse()
        return response.status, response.read(MAX_ANSWER_BYTES)
    except TimeoutError:
        raise UploadError(f"{site} did not answer within {timeout:g} s") from None
    except (OSError, http.client.HTTPException) as error:
        raise UploadError(f"no answer from {site}: {_error_text(error)}") from None


def upload(site: str, page: str, tar_path: Path, sha256: str, token: str, *, connect=http.client.HTTPSConnection,
           timeout: float = UPLOAD_TIMEOUT_SECONDS) -> dict:
    """Sends `tar_path` to `<site>/api/v1/builds/<page>` and returns the site's JSON answer of a 200. `connect(site, timeout=timeout)`
    makes the connection (a test hands a plain one to a local server). The body is streamed from the file with a known Content-Length,
    never chunked; a redirect is not followed, so the token goes only to `site`. Any other status is an UploadError with the status and
    the site's `detail`."""
    if not PAGE_PATTERN.fullmatch(page):
        raise UploadError(f"{page!r} is not a page name")
    if not SHA256_PATTERN.fullmatch(sha256):
        raise UploadError("the sha256 is not 64 lower-case hex digits")
    if not TOKEN_PATTERN.fullmatch(token):
        raise UploadError(f"the {ENV_VARIABLE} value is not usable: it must be printable ASCII with no space or control character")
    tar_path = Path(tar_path)
    try:
        size = tar_path.stat().st_size
    except OSError as error:
        raise UploadError(f"cannot read {tar_path}: {type(error).__name__}") from None

    connection = connect(site, timeout=timeout)
    try:
        sent_all = False
        try:
            connection.putrequest("PUT", BUILDS_PATH + page)
            connection.putheader("Authorization", f"Bearer {token}")
            connection.putheader("Content-Type", "application/x-tar")
            connection.putheader("Content-Length", str(size))
            connection.putheader("X-Content-Sha256", sha256)
            connection.endheaders()
            sent = 0
            with tar_path.open("rb") as handle:
                while sent < size:
                    piece = handle.read(PIECE_BYTES)
                    if not piece:
                        break
                    connection.send(piece)
                    sent += len(piece)
            sent_all = sent == size
        except TimeoutError:
            raise UploadError(f"sending to {site} stopped: no progress within {timeout:g} s") from None
        except OSError as error:
            # The site may have answered before the body ended (401, 409 and 413 do) and closed: that answer says why, a broken pipe
            # does not. Socket readability is no sign of an answer: TLS 1.3 sends session tickets after the handshake.
            if connection.sock is None:
                raise UploadError(f"cannot connect to {site}: {_error_text(error)}") from None
            try:
                status, body = _answer(connection, site, token, timeout)
            except UploadError:
                raise UploadError(f"the connection to {site} broke while sending: {_error_text(error)}") from None
            raise UploadError(f"the site refused the upload ({status}): {_detail(body, token)}") from None

        status, body = _answer(connection, site, token, timeout)
        if status != 200:
            raise UploadError(f"the site refused the upload ({status}): {_detail(body, token)}")
        if not sent_all:
            raise UploadError(f"the site answered 200 before the whole package was sent: {_detail(body, token)}")
        try:
            answer = json.loads(body)
        except ValueError:
            answer = None
        if not isinstance(answer, dict):
            raise UploadError("the site answered 200 with something that is no JSON object")
        return answer
    finally:
        connection.close()
