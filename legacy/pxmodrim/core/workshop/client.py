from __future__ import annotations

from urllib.parse import quote

import httpx
import msgspec

from pxmodrim.core.workshop.types import (
    CatalogCollection,
    CatalogMod,
    CatalogPage,
    CatalogQuery,
    CollectionDetail,
    Discover,
    ResolvedDownload,
)


class CatalogError(Exception):
    def __init__(self, message: str, status: int | None = None) -> None:
        super().__init__(message)
        self.message = message
        self.status = status


class _ErrorMessage(msgspec.Struct):
    message: str


class _ErrorEnvelope(msgspec.Struct):
    error: _ErrorMessage


class _Batch(msgspec.Struct):
    items: list[CatalogMod]
    unavailable_ids: list[str]


class CatalogClient:
    def __init__(
        self, base_url: str, transport: httpx.AsyncBaseTransport | None = None
    ) -> None:
        self._base_url = base_url.strip().rstrip("/")
        self._http = httpx.AsyncClient(transport=transport, timeout=30.0)

    async def shutdown(self) -> None:
        await self._http.aclose()

    async def _request[T](
        self,
        method: str,
        path: str,
        response_type: type[T],
        *,
        params: dict[str, str | int] | None = None,
        payload: dict[str, list[str]] | None = None,
    ) -> T:
        if not self._base_url:
            raise CatalogError("Workshop catalog is disabled.")
        try:
            response = await self._http.request(
                method,
                f"{self._base_url}/catalog/{path}",
                params=params,
                json=payload,
                timeout=30.0,
            )
        except (httpx.HTTPError, httpx.InvalidURL) as exc:
            raise CatalogError(f"Cannot reach the Workshop catalog: {exc}") from exc
        if not response.is_success:
            try:
                error = msgspec.json.decode(response.content, type=_ErrorEnvelope)
                message = error.error.message
            except msgspec.DecodeError:
                message = (
                    f"Workshop catalog request failed "
                    f"(HTTP {response.status_code} {response.reason_phrase})."
                )
            raise CatalogError(message, response.status_code)
        try:
            return msgspec.json.decode(response.content, type=response_type)
        except msgspec.DecodeError as exc:
            raise CatalogError(
                "The Workshop catalog returned an invalid response.",
                response.status_code,
            ) from exc

    @staticmethod
    def _params(query: CatalogQuery) -> dict[str, str | int]:
        params: dict[str, str | int] = {
            "q": query.query,
            "sort": query.sort,
            "limit": query.limit,
            "source": query.source,
        }
        for key, value in (
            ("tag", query.tag),
            ("version", query.version),
            ("cursor", query.cursor),
        ):
            if value is not None:
                params[key] = value
        return params

    async def discover(self) -> Discover:
        return await self._request("GET", "discover", Discover)

    async def mods(self, q: CatalogQuery) -> CatalogPage[CatalogMod]:
        return await self._request(
            "GET", "mods", CatalogPage[CatalogMod], params=self._params(q)
        )

    async def collections(self, q: CatalogQuery) -> CatalogPage[CatalogCollection]:
        return await self._request(
            "GET", "collections", CatalogPage[CatalogCollection], params=self._params(q)
        )

    async def mod(self, id: str) -> CatalogMod | None:
        try:
            return await self._request("GET", f"mods/{quote(id, safe='')}", CatalogMod)
        except CatalogError as exc:
            if exc.status == 404:
                return None
            raise

    async def collection(self, id: str) -> CollectionDetail | None:
        source, separator, key = id.partition(":")
        if not separator or source not in {"steam", "picked"} or not key:
            raise CatalogError("Invalid Workshop collection ID.")
        try:
            return await self._request(
                "GET", f"collections/{source}/{quote(key, safe='')}", CollectionDetail
            )
        except CatalogError as exc:
            if exc.status == 404:
                return None
            raise

    async def mods_batch(self, ids: list[str]) -> tuple[list[CatalogMod], list[str]]:
        items: list[CatalogMod] = []
        unavailable: list[str] = []
        for start in range(0, len(ids), 100):
            batch = await self._request(
                "POST", "mods/batch", _Batch, payload={"ids": ids[start : start + 100]}
            )
            items.extend(batch.items)
            unavailable.extend(batch.unavailable_ids)
        return items, unavailable

    async def resolve(
        self, ids: list[str], collection_ids: list[str]
    ) -> ResolvedDownload:
        roots: dict[str, None] = {}
        items: dict[str, CatalogMod | CatalogCollection] = {}
        mod_ids: dict[str, None] = {}
        unavailable: dict[str, None] = {}
        incomplete: dict[str, None] = {}
        is_complete = True
        batches = max((len(ids) + 49) // 50, (len(collection_ids) + 9) // 10, 1)
        for batch in range(batches):
            resolved = await self._request(
                "POST",
                "resolve",
                ResolvedDownload,
                payload={
                    "ids": ids[batch * 50 : (batch + 1) * 50],
                    "collection_ids": collection_ids[batch * 10 : (batch + 1) * 10],
                },
            )
            roots.update(dict.fromkeys(resolved.roots))
            items.update(resolved.items)
            mod_ids.update(dict.fromkeys(resolved.mod_ids))
            unavailable.update(dict.fromkeys(resolved.unavailable_ids))
            incomplete.update(dict.fromkeys(resolved.incomplete_collection_ids))
            is_complete = is_complete and resolved.is_complete
        return ResolvedDownload(
            roots=list(roots),
            items=items,
            mod_ids=list(mod_ids),
            unavailable_ids=list(unavailable),
            incomplete_collection_ids=list(incomplete),
            is_complete=is_complete,
        )
