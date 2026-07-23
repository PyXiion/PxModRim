// ============================================================================
// GLOBAL RUNTIME CACHE & CONFIG
// ============================================================================

class LRUMap {
  constructor(maxSize = 10000) {
    this._max = maxSize;
    this._map = new Map();
  }
  get(key) {
    const val = this._map.get(key);
    if (val !== undefined) {
      this._map.delete(key);
      this._map.set(key, val);
    }
    return val;
  }
  set(key, value) {
    if (this._map.has(key)) this._map.delete(key);
    else if (this._map.size >= this._max) this._map.delete(this._map.keys().next().value);
    this._map.set(key, value);
  }
  has(key) { return this._map.has(key); }
  get size() { return this._map.size; }
}

const GLOBAL_RAM_CACHE = new LRUMap(10000);

const CACHE_TTL_OK_MOD = 72 * 60 * 60 * 1000;
const CACHE_TTL_OK_COLLECTION = 1 * 60 * 60 * 1000;
const CACHE_TTL_ERROR = 1 * 60 * 60 * 1000;
const QUEUE_DEDUP_TTL_MS = 60 * 1000;
const MAX_INLINE_SCRAPE = 200*4;
const STEAM_API_BATCH_SIZE = 200;
const QUERY_MODS_PAGE_SIZE = 3000;
const CRAWL_BATCH_SIZE = 200;

const RECENTLY_QUEUED = new Map();

function sweepRecentlyQueued() {
  const cutoff = Date.now() - QUEUE_DEDUP_TTL_MS;
  for (const [id, ts] of RECENTLY_QUEUED) {
    if (ts < cutoff) RECENTLY_QUEUED.delete(id);
  }
}

// ============================================================================
// 1. PARSERS
// ============================================================================

function parseRequiredDeps(htmlBlock) {
  const deps = [];
  const seenIds = new Set();
  const reqContainerMatch = htmlBlock.match(/<div[^>]*id=["']RequiredItems["'][^>]*>([\s\S]*?)<\/div>\s*<\/div>/i);
  if (!reqContainerMatch) return deps;

  const regex = /href=["'][^"']*filedetails\/\?id=(\d+)[^"']*/gi;
  let match;

  while ((match = regex.exec(reqContainerMatch[1])) !== null) {
    if (!seenIds.has(match[1])) {
      seenIds.add(match[1]);
      deps.push(match[1]);
    }
  }
  return deps;
}

// ============================================================================
// 2. NETWORK & D1 DATABASE
// ============================================================================

async function fetchSteamPageHtml(apiKey, modId) {
  const targetUrl = `https://steamcommunity.com/sharedfiles/filedetails/?id=${modId}`;
  const apiUrl = `https://api.scrapingant.com/v2/general?url=${encodeURIComponent(targetUrl)}&browser=false`;

  const startTime = Date.now();
  try {
    const response = await fetch(apiUrl, {
      method: 'GET',
      headers: {
        'x-api-key': apiKey,
        'Ant-Cookies': 'wants_mature_content=1; birthtime=283993201; lastagecheckage=1-0-1990'
      }
    });

    const duration = Date.now() - startTime;
    if (!response.ok) {
      const errText = await response.text();
      console.error(`[SCRAPINGANT FAIL] ID ${modId} | HTTP ${response.status} (${duration}ms) | ${errText.slice(0, 150)}`);
      return { html: null, httpStatus: response.status, error: errText };
    }

    const html = await response.text();
    return { html, httpStatus: 200, error: null };
  } catch (err) {
    console.error(`[NETWORK EXCEPTION] ID ${modId}:`, err.message || err);
    return { html: null, httpStatus: 0, error: err.message };
  }
}

async function saveToDB(db, modId, title, isCollection, status, deps, timestamp, packageId = '') {
  try {
    const depsStr = Array.isArray(deps) ? deps.join(',') : '';
    await db.prepare(
      `INSERT INTO items (id, title, is_collection, status, updated_at, package_id, deps)
       VALUES (?, ?, ?, ?, ?, ?, ?)
       ON CONFLICT(id) DO UPDATE SET
         title = excluded.title,
         is_collection = excluded.is_collection,
         status = excluded.status,
         updated_at = excluded.updated_at,
         package_id = excluded.package_id,
         deps = excluded.deps`
    ).bind(modId, title, isCollection ? 1 : 0, status, timestamp, packageId, depsStr).run();
  } catch (err) {
    console.error(`[D1 SAVE ERROR] ID ${modId}:`, err.message || err);
  }
}

async function prewarmCacheFromD1(db, rootId) {
  const toLoad = new Set([rootId]);
  const loaded = new Set();
  let loadedCount = 0;

  while (toLoad.size > 0) {
    const batch = [...toLoad].slice(0, 200);
    for (const id of batch) {
      toLoad.delete(id);
      loaded.add(id);
    }

    for (let offset = 0; offset < batch.length; offset += 99) {
      const chunk = batch.slice(offset, offset + 99);
      const placeholders = chunk.map(() => '?').join(',');
      const { results } = await db.prepare(
        `SELECT id, title, is_collection, status, updated_at, package_id, deps
         FROM items WHERE id IN (${placeholders})`
      ).bind(...chunk).all();

      if (!results) continue;

      for (const row of results) {
        const deps = row.deps ? row.deps.split(',') : [];
        GLOBAL_RAM_CACHE.set(row.id, {
          data: {
            id: row.id,
            title: row.title,
            is_collection: Boolean(row.is_collection),
            is_available: row.status === 'OK',
            status: row.status,
            deps,
            package_id: row.package_id || ''
          },
          timestamp: row.updated_at,
          status: row.status
        });
        loadedCount++;

        for (const dep of deps) {
          if (!GLOBAL_RAM_CACHE.has(dep) && !loaded.has(dep)) {
            toLoad.add(dep);
          }
        }
      }
    }
  }

  if (loadedCount === 0) {
    console.log(`[D1 PREWARM] No cached items found for root ${rootId}`);
  } else {
    console.log(`[D1 PREWARM] Loaded ${loadedCount} cached nodes for root ${rootId}`);
  }
}

function buildTree(targetId) {
  const itemsMap = {};
  const queue = [targetId];
  const visited = new Set();
  const missingIds = new Set();
  const now = Date.now();

  while (queue.length > 0) {
    const currentId = queue.shift();
    if (visited.has(currentId)) continue;
    visited.add(currentId);

    if (GLOBAL_RAM_CACHE.has(currentId)) {
      const cached = GLOBAL_RAM_CACHE.get(currentId);
      const ttl = cached.status === 'OK'
        ? (cached.data.is_collection ? CACHE_TTL_OK_COLLECTION : CACHE_TTL_OK_MOD)
        : CACHE_TTL_ERROR;

      if ((now - cached.timestamp) < ttl && cached.status !== 'PENDING') {
        itemsMap[currentId] = cached.data;
        if (Array.isArray(cached.data.deps)) {
          queue.push(...cached.data.deps);
        }
        continue;
      }

      if (Array.isArray(cached.data.deps)) {
        queue.push(...cached.data.deps);
      }

      missingIds.add(currentId);
    } else {
      missingIds.add(currentId);
    }
  }

  return { itemsMap, missingIds };
}

async function fetchSteamApiDetails(apiKey, modIds) {
  const jsonPayload = {
    publishedfileids: modIds,
    includechildren: true,
    includetags: false,
    includekvtags: true
  };
  const url = `https://api.steampowered.com/IPublishedFileService/GetDetails/v1/?key=${apiKey}&input_json=${encodeURIComponent(JSON.stringify(jsonPayload))}`;

  const startTime = Date.now();
  try {
    const response = await fetch(url, { method: 'GET' });
    const duration = Date.now() - startTime;

    if (response.status === 429) {
      console.warn(`[STEAM API RATE LIMITED] Batch of ${modIds.length} IDs returned 429`);
      return null;
    }

    if (!response.ok) {
      console.error(`[STEAM API FAIL] HTTP ${response.status} (${duration}ms)`);
      return null;
    }

    const data = await response.json();
    const details = data?.response?.publishedfiledetails;
    if (!Array.isArray(details)) {
      console.error(`[STEAM API FAIL] Unexpected response shape`);
      return null;
    }

    return details;
  } catch (err) {
    console.error(`[STEAM API EXCEPTION] Batch of ${modIds.length}:`, err.message || err);
    return null;
  }
}

function processApiResults(details) {
  const processed = [];

  for (const detail of details) {
    const id = detail.publishedfileid;
    let status, title, isCollection, deps, packageId;

    if (detail.result !== 1) {
      status = detail.result === 15 ? 'PRIVATE' : `ERROR_RESULT_${detail.result}`;
      title = `Unavailable (${status})`;
      isCollection = false;
      deps = [];
      packageId = '';
    } else {
      status = 'OK';
      title = detail.title || 'Untitled';
      isCollection = (detail.file_type && detail.file_type !== 0);
      deps = (detail.children || []).map(c => c.publishedfileid).filter(Boolean);
      const kvTag = (detail.kvtags || []).find(t => t.key === 'packageId');
      packageId = kvTag ? kvTag.value : '';
    }

    processed.push({
      id, title, is_collection: isCollection, is_available: status === 'OK', status, deps, package_id: packageId
    });
  }

  return { processed };
}

// ============================================================================
// 4. CRAWL — STALE MOD DISCOVERY VIA QueryFiles
// ============================================================================

async function queryMods(apiKey, cursor) {
  const params = new URLSearchParams({
    key: apiKey,
    query_type: '1',
    page: '1',
    cursor: cursor,
    numperpage: String(QUERY_MODS_PAGE_SIZE),
    creator_appid: '294100',
    appid: '294100',
    filetype: '0',
    ids_only: 'true',
    return_children: 'false',
    return_vote_data: 'false',
    return_tags: 'false',
    return_kv_tags: 'false',
    return_previews: 'false',
    return_short_description: 'false',
    return_metadata: 'false',
    return_playtime_stats: 'false'
  });
  const url = `https://api.steampowered.com/IPublishedFileService/QueryFiles/v1/?${params.toString()}`;

  const response = await fetch(url, { method: 'GET' });
  if (!response.ok) {
    console.error(`[CRAWL] QueryFiles HTTP ${response.status}`);
    return { ids: [], next_cursor: '' };
  }

  const data = await response.json();
  const details = data?.response?.publishedfiledetails;
  const nextCursor = data?.response?.next_cursor || '';

  const ids = Array.isArray(details) ? details.map(d => d.publishedfileid).filter(Boolean) : [];

  return { ids, next_cursor: nextCursor };
}

async function findStaleIds(db, ids) {
  const stale = [];
  const now = Date.now();

  for (let i = 0; i < ids.length; i += 99) {
    const batch = ids.slice(i, i + 99);
    const placeholders = batch.map(() => '?').join(',');
    const { results } = await db.prepare(
      `SELECT id, is_collection, updated_at FROM items WHERE id IN (${placeholders})`
    ).bind(...batch).all();

    const found = new Map((results || []).map(r => [r.id, r]));

    for (const id of batch) {
      const row = found.get(id);
      if (row === undefined) {
        stale.push(id);
      } else {
        const ttl = row.is_collection ? CACHE_TTL_OK_COLLECTION : CACHE_TTL_OK_MOD;
        if (row.updated_at < now - ttl) {
          stale.push(id);
        }
      }
    }
  }

  return stale;
}

async function getCrawlState(db) {
  const { results } = await db.prepare(
    'SELECT cursor, todo_ids, total_processed FROM crawl_state WHERE id = 1'
  ).all();

  if (!results || results.length === 0) {
    await db.prepare(
      `INSERT INTO crawl_state (id, cursor, todo_ids, total_processed, updated_at) VALUES (1, '*', '', 0, 0)`
    ).run();
    return { cursor: '*', todo_ids: [], total_processed: 0 };
  }

  return {
    cursor: results[0].cursor || '*',
    todo_ids: results[0].todo_ids ? results[0].todo_ids.split(',').filter(Boolean) : [],
    total_processed: results[0].total_processed || 0
  };
}

async function saveCrawlState(db, state) {
  await db.prepare(
    'UPDATE crawl_state SET cursor = ?, todo_ids = ?, total_processed = ?, updated_at = ? WHERE id = 1'
  ).bind(
    state.cursor,
    state.todo_ids.join(','),
    state.total_processed,
    Date.now()
  ).run();
}

async function crawlTick(env) {
  if (!env.STEAM_API_KEY) {
    console.warn('[CRAWL] No STEAM_API_KEY, skipping tick');
    return;
  }

  const state = await getCrawlState(env.DB);

  if (state.todo_ids.length === 0) {
    console.log(`[CRAWL] Fetching page, cursor=${state.cursor || '(start)'}`);

    const { ids, next_cursor } = await queryMods(env.STEAM_API_KEY, state.cursor);

    if (ids.length === 0) {
      console.log('[CRAWL] Empty page, resetting cursor');
      state.cursor = '*';
      state.todo_ids = [];
      await saveCrawlState(env.DB, state);
      return;
    }

    const staleIds = await findStaleIds(env.DB, ids);
    console.log(`[CRAWL] Page: ${ids.length} IDs, ${staleIds.length} need scraping`);

    state.cursor = next_cursor;
    state.todo_ids = staleIds;
  }

  if (state.todo_ids.length === 0) {
    if (!state.cursor) {
      state.cursor = '*';
      console.log(`[CRAWL] Cycle complete. Total processed: ${state.total_processed}. Restarting.`);
    }
    await saveCrawlState(env.DB, state);
    return;
  }

  const batchSize = Math.min(CRAWL_BATCH_SIZE, state.todo_ids.length);
  const batch = state.todo_ids.splice(0, batchSize);

  const missingSet = new Set(batch);
  await scrapeInline(env, missingSet);

  state.total_processed += batchSize;
  console.log(`[CRAWL] Scraped ${batchSize} mods, ${state.todo_ids.length} remaining on page, total: ${state.total_processed}`);

  if (state.todo_ids.length === 0 && !state.cursor) {
    state.cursor = '*';
    console.log(`[CRAWL] Cycle complete. Total processed: ${state.total_processed}. Restarting.`);
  }

  await saveCrawlState(env.DB, state);
}

async function ensureCollectionRootDeps(env, rootId) {
  const cached = GLOBAL_RAM_CACHE.get(rootId);
  if (!cached || !cached.data.is_collection) return false;

  const details = await fetchSteamApiDetails(env.STEAM_API_KEY, [rootId]);
  if (!details) return false;

  const rootDetail = details.find(d => d.publishedfileid === rootId);
  if (!rootDetail || rootDetail.result !== 1) return false;

  const children = (rootDetail.children || []).map(c => c.publishedfileid).filter(Boolean);
  if (children.length === 0) return false;

  const currentDeps = cached.data.deps || [];
  const depsChanged = children.length !== currentDeps.length ||
    children.some((id, i) => id !== currentDeps[i]);
  if (!depsChanged) return false;

  const now = Date.now();
  await saveToDB(env.DB, rootId, cached.data.title, true, 'OK', children, now, cached.data.package_id || '');

  GLOBAL_RAM_CACHE.set(rootId, {
    data: { ...cached.data, deps: children },
    timestamp: now,
    status: 'OK'
  });

  console.log(`[ROOT COLLECTION] Updated root ${rootId} with ${children.length} children`);
  return true;
}

async function scrapeViaScrapingAnt(env, modId) {
  const now = Date.now();
  console.log(`[SCRAPINGANT FALLBACK] Scraping Steam page for ID: ${modId}...`);

  const { html, httpStatus, error } = await fetchSteamPageHtml(env.SCRAPI, modId);

  if (httpStatus === 409 || httpStatus === 429) {
    console.warn(`[SCRAPINGANT FALLBACK] Rate limited on ${modId}`);
    return { isRateLimited: true };
  }

  let resultStatus = 'OK';
  let resultData = null;

  if (!html) {
    resultStatus = `SCRAPE_ERROR_HTTP_${httpStatus}`;
    resultData = {
      id: modId,
      title: "Failed to Scrape",
      is_available: false,
      status: resultStatus,
      error_details: `ScrapingAnt HTTP ${httpStatus}: ${error || 'Unknown error'}`,
      deps: []
    };
  } else if (html.includes('class="error_ctn"') || html.includes("There was an error trying to handle your request")) {
    const errorBlockMatch = html.match(/class="error_ctn"[^>]*>([\s\S]*?)<\/div>/i);
    const errorBlock = errorBlockMatch ? errorBlockMatch[1] : '';
    resultStatus = errorBlock.includes('hidden') || errorBlock.includes('private') ? 'PRIVATE' : 'DELETED';
    resultData = {
      id: modId,
      title: `Unavailable (${resultStatus})`,
      is_available: false,
      status: resultStatus,
      deps: []
    };
  } else {
    const titleMatch = html.match(/<div class="workshopItemTitle">([^<]+)<\/div>/i);
    let title = titleMatch?.[1]?.replace(/&amp;/g, '&')?.trim();

    if (!title) {
      const ogTitleMatch = html.match(/<meta property="og:title" content="([^"]+)">/i);
      if (ogTitleMatch && !ogTitleMatch[1].includes("Steam Community :: Error")) {
        title = ogTitleMatch[1].replace("Steam Workshop::", "").trim();
      }
    }

    if (!title) {
      resultStatus = 'INVALID_PAGE';
      resultData = { id: modId, title: "Invalid Steam Page", is_available: false, status: resultStatus, deps: [] };
    } else {
      const isCollection = html.includes('class="collectionChildren"') || html.includes('id="CollectionItems"');
      const deps = isCollection ? [] : parseRequiredDeps(html);

      resultStatus = 'OK';
      resultData = { id: modId, title, is_collection: isCollection, is_available: true, status: 'OK', deps, package_id: '' };
    }
  }

  await saveToDB(env.DB, modId, resultData.title, resultData.is_collection || false, resultStatus, resultData.deps || [], now, resultData.package_id || '');
  GLOBAL_RAM_CACHE.set(modId, { data: resultData, timestamp: now, status: resultStatus });

  return { data: resultData, nextTasks: resultData.deps || [] };
}

async function scrapeInline(env, missingIds, rootId = null) {
  const toScrape = [...missingIds];
  const inQueue = new Set(missingIds);
  const scraped = new Set();
  let scrapedCount = 0;

  while (toScrape.length > 0 && scrapedCount < MAX_INLINE_SCRAPE) {
    const batch = [];
    while (batch.length < STEAM_API_BATCH_SIZE && toScrape.length > 0 && scrapedCount + batch.length < MAX_INLINE_SCRAPE) {
      const id = toScrape.shift();
      if (!scraped.has(id)) batch.push(id);
    }
    if (batch.length === 0) break;

    const apiDetails = await fetchSteamApiDetails(env.STEAM_API_KEY, batch);

    if (!apiDetails) {
      for (const id of batch) {
        if (scrapedCount >= MAX_INLINE_SCRAPE) break;
        const result = await scrapeViaScrapingAnt(env, id);
        if (result.isRateLimited) {
          for (const remaining of toScrape) scraped.add(remaining);
          break;
        }
        scraped.add(id);
        scrapedCount++;
        if (result.nextTasks) {
          for (const dep of result.nextTasks) {
            if (!scraped.has(dep) && !inQueue.has(dep)) {
              toScrape.push(dep);
              inQueue.add(dep);
            }
          }
        }
      }
      continue;
    }

    const { processed } = processApiResults(apiDetails);

    const respondedIds = new Set(processed.map(m => m.id));
    for (const id of batch) {
      if (!respondedIds.has(id)) {
        processed.push({
          id,
          title: 'Deleted',
          is_collection: false,
          is_available: false,
          status: 'DELETED',
          deps: [],
          package_id: ''
        });
      }
    }

    for (const mod of processed) {
      if (scrapedCount >= MAX_INLINE_SCRAPE) break;
      if (scraped.has(mod.id)) continue;

      const keepDeps = !mod.is_collection || mod.id === rootId;
      const depsToSave = keepDeps ? mod.deps : [];

      const now = Date.now();
      await saveToDB(env.DB, mod.id, mod.title, mod.is_collection, mod.status, depsToSave, now, mod.package_id);
      const ramData = {
        id: mod.id,
        title: mod.title,
        is_collection: mod.is_collection,
        is_available: mod.is_available,
        status: mod.status,
        deps: depsToSave,
        package_id: mod.package_id
      };
      GLOBAL_RAM_CACHE.set(mod.id, { data: ramData, timestamp: now, status: mod.status });
      scraped.add(mod.id);
      scrapedCount++;

      if (depsToSave.length > 0) {
        for (const dep of depsToSave) {
          if (!scraped.has(dep) && !inQueue.has(dep)) {
            toScrape.push(dep);
            inQueue.add(dep);
          }
        }
      }
    }
  }

  const stillMissing = new Set();
  for (const id of missingIds) {
    if (!scraped.has(id)) stillMissing.add(id);
  }
  return stillMissing;
}

// ============================================================================
// 5. ROUTER & QUEUE CONSUMER
// ============================================================================

export default {
  async fetch(request, env, ctx) {
    try {
      const url = new URL(request.url);

      if (url.pathname === '/') {
        return new Response('ok', {
          status: 200,
          headers: { 'Content-Type': 'text/plain' }
        });
      }

      if (url.pathname === '/purge') {
        const key = url.searchParams.get('key');
        let validKeys = [];
        try { validKeys = env.CONTROL_KEYS ? JSON.parse(env.CONTROL_KEYS) : []; } catch {}
        if (!key || !Array.isArray(validKeys) || !validKeys.includes(key)) {
          return new Response('Unauthorized', { status: 401 });
        }

        const id = url.searchParams.get('id');
        if (id && /^\d+$/.test(id.trim())) {
          const purgeUrl = new URL(`/deps?id=${id.trim()}`, url.origin);
          await caches.default.delete(new Request(purgeUrl));
          console.log(`[PURGE] Cleared cache for mod ${id.trim()}`);
          return new Response(`Purged ${id.trim()}`, { status: 200 });
        }
        return new Response('Provide ?id=<steamid> to purge a specific mod', { status: 400 });
      }

      if (url.pathname === '/deps') {
        const rootId = url.searchParams.get('id');

        if (!rootId || !/^\d+$/.test(rootId.trim())) {
          return new Response(JSON.stringify({ error: 'Provide a valid "id" query parameter' }), {
            status: 400,
            headers: { 'Content-Type': 'application/json' }
          });
        }

        const targetId = rootId.trim();
        const cacheKey = new Request(url);
        const cachedResponse = await caches.default.match(cacheKey);
        if (cachedResponse) return cachedResponse;

        console.log(`[REQUEST START] Processing graph for root ID: ${targetId}`);

        try {
          sweepRecentlyQueued();
          await prewarmCacheFromD1(env.DB, targetId);
          await ensureCollectionRootDeps(env, targetId);

          const useQueue = env.SCRAPE_QUEUE && env.USE_QUEUE === 'true';

          if (!env.STEAM_API_KEY) {
            console.warn('[CONFIG] STEAM_API_KEY not set — Steam API calls will fall back to ScrapingAnt');
          }

          let result = buildTree(targetId);
          let itemsMap = result.itemsMap;
          let missingIds = result.missingIds;

          if (missingIds.size > 0) {
            let passes = 0;

            while (missingIds.size > 0 && passes < 3) {
              passes++;
              console.log(`[INLINE SCRAPE] Pass ${passes}: ${missingIds.size} missing IDs`);
              await scrapeInline(env, missingIds, targetId);
              await prewarmCacheFromD1(env.DB, targetId);
              await ensureCollectionRootDeps(env, targetId);
              const rebased = buildTree(targetId);
              Object.assign(itemsMap, rebased.itemsMap);
              missingIds = rebased.missingIds;
            }

            if (useQueue && missingIds.size > 0) {
              try {
                if (!RECENTLY_QUEUED.has(targetId)) {
                  await env.SCRAPE_QUEUE.send({ body: { rootId: targetId } });
                  RECENTLY_QUEUED.set(targetId, Date.now());
                  console.log(`[QUEUE BG] Enqueued root ${targetId} (${missingIds.size} IDs remaining)`);
                }
              } catch (queueErr) {
                console.warn(`[QUEUE FAIL] ${queueErr.message}`);
              }
            }
          }

          const isComplete = missingIds.size === 0;

          const responseBody = JSON.stringify({
            rootId: targetId,
            totalItemsLoaded: Object.keys(itemsMap).length,
            isComplete,
            items: itemsMap
          }, null, 2);

          if (isComplete) {
            const response = new Response(responseBody, {
              status: 200,
              headers: {
                'Content-Type': 'application/json',
                'Cache-Control': 'public, s-maxage=1800'
              }
            });
            ctx.waitUntil(caches.default.put(cacheKey, response.clone()));
            return response;
          }

          return new Response(responseBody, {
            status: 200,
            headers: { 'Content-Type': 'application/json' }
          });

        } catch (err) {
          console.error(`[FATAL ERROR]`, err.stack || err);
          return new Response(JSON.stringify({ error: 'Internal Server Error', message: err.message }), {
            status: 500,
            headers: { 'Content-Type': 'application/json' }
          });
        }
      }

      return new Response('Not Found', { status: 404 });
    } catch (err) {
      console.error('[CRITICAL WORKER ERROR]', err.stack || err);
      return new Response(JSON.stringify({
        error: 'Worker Execution Error',
        message: err.message
      }), {
        status: 500,
        headers: { 'Content-Type': 'application/json' }
      });
    }
  },

  async scheduled(event, env, ctx) {
    ctx.waitUntil(crawlTick(env));
  },

  async queue(batch, env) {
    console.log(`[QUEUE CONSUMER] Received batch of ${batch.messages.length} items`);
    sweepRecentlyQueued();

    for (const message of batch.messages) {
      const { rootId } = message.body;

      await prewarmCacheFromD1(env.DB, rootId);
      await ensureCollectionRootDeps(env, rootId);
      const { missingIds } = buildTree(rootId);

      if (missingIds.size === 0) {
        console.log(`[QUEUE COMPLETE] Tree for root ${rootId} fully cached, acking`);
        message.ack();
        continue;
      }

      console.log(`[QUEUE SCRAPE] Root ${rootId}: ${missingIds.size} missing IDs`);

      try {
        await scrapeInline(env, missingIds, rootId);
        await prewarmCacheFromD1(env.DB, rootId);
        await ensureCollectionRootDeps(env, rootId);

        const check = buildTree(rootId);
        if (check.missingIds.size === 0) {
          console.log(`[QUEUE COMPLETE] Root ${rootId} fully resolved`);
          message.ack();
        } else {
          console.log(`[QUEUE CONTINUE] Root ${rootId}: ${check.missingIds.size} still missing, re-scheduling`);
          message.ack();
          await env.SCRAPE_QUEUE.send(
            { body: { rootId } },
            { deliveryDelay: 30 }
          );
        }
      } catch (err) {
        console.error(`[QUEUE ERROR] Root ${rootId}:`, err.message || err);
        message.retry();
      }
    }
  }
};
