#!/usr/bin/env python3
import asyncio
import json
import os
import sys
import time
import traceback
from aiohttp import web, ClientSession, ClientTimeout, ClientError

LISTEN_HOST = os.environ.get('LISTEN_HOST', '127.0.0.1')
LISTEN_PORT = int(os.environ.get('LISTEN_PORT', '3100'))
UPSTREAM = os.environ.get('UPSTREAM', 'http://127.0.0.1:3000').rstrip('/')
TARGET_NAME = os.environ.get('TARGET_NAME', 'bb')
OUT_FILE = os.environ.get('OUT_FILE', './results/_logs/http_events.jsonl')
LOG_FILE = os.environ.get('LOG_FILE', './results/_logs/http_byte_proxy.log')
ERR_FILE = os.environ.get('ERR_FILE', './results/_logs/http_byte_proxy.err.log')
PROXY_TIMEOUT_SEC = float(os.environ.get('PROXY_TIMEOUT_SEC', '30'))
MAX_BODY_MB = int(os.environ.get('MAX_BODY_MB', '64'))

os.makedirs(os.path.dirname(OUT_FILE), exist_ok=True)
os.makedirs(os.path.dirname(LOG_FILE), exist_ok=True)
os.makedirs(os.path.dirname(ERR_FILE), exist_ok=True)

def append_line(path, line):
    with open(path, 'a', encoding='utf-8') as f:
        f.write(line + '\n')

def log(msg):
    line = f"[http_byte_proxy][{time.strftime('%Y-%m-%dT%H:%M:%S')}] {msg}"
    print(line, file=sys.stderr, flush=True)
    append_line(LOG_FILE, line)

def log_err(msg, extra=None):
    payload = {'ts': int(time.time() * 1000), 'type': 'error', 'msg': msg}
    if extra:
        payload.update(extra)
    line = f"[http_byte_proxy][{time.strftime('%Y-%m-%dT%H:%M:%S')}][ERROR] {msg}"
    if extra:
        line += ' ' + json.dumps(extra, ensure_ascii=False)
    print(line, file=sys.stderr, flush=True)
    append_line(ERR_FILE, line)
    append_line(OUT_FILE, json.dumps(payload, ensure_ascii=False))

def append_event(obj):
    append_line(OUT_FILE, json.dumps(obj, ensure_ascii=False))

async def proxy_health(request: web.Request):
    return web.json_response({
        'ok': 1,
        'service': 'http_byte_proxy',
        'target': TARGET_NAME,
        'listen': f'{LISTEN_HOST}:{LISTEN_PORT}',
        'upstream': UPSTREAM,
        'ts': int(time.time() * 1000),
    })

async def proxy_upstream_health(request: web.Request):
    started = time.time()
    try:
        async with request.app['client'].get(f"{UPSTREAM}/health") as resp:
            body = await resp.read()
            return web.json_response({
                'ok': 1 if resp.status < 500 else 0,
                'service': 'http_byte_proxy',
                'target': TARGET_NAME,
                'upstream': UPSTREAM,
                'upstream_status': resp.status,
                'upstream_body_preview': body[:300].decode('utf-8', errors='replace'),
                'latency_ms': int((time.time() - started) * 1000),
                'ts': int(time.time() * 1000),
            }, status=200 if resp.status < 500 else 502)
    except Exception as err:
        detail = {
            'target': TARGET_NAME,
            'upstream': UPSTREAM,
            'err': str(err),
            'trace': traceback.format_exc()[-1600:],
        }
        log_err('upstream_health_error', detail)
        return web.json_response({
            'ok': 0,
            'service': 'http_byte_proxy',
            'target': TARGET_NAME,
            'upstream': UPSTREAM,
            'err': str(err),
            'ts': int(time.time() * 1000),
        }, status=502)

async def handle(request: web.Request):
    started = time.time()
    req_body = b''
    try:
        req_body = await request.read()
        upstream_url = f"{UPSTREAM}{request.rel_url}"
        headers = dict(request.headers)
        headers.pop('Host', None)
        headers.pop('Content-Length', None)
        timeout = ClientTimeout(total=PROXY_TIMEOUT_SEC)
        async with request.app['client'].request(
            method=request.method,
            url=upstream_url,
            headers=headers,
            data=req_body,
            allow_redirects=False,
            timeout=timeout,
        ) as resp:
            resp_body = await resp.read()
            latency_ms = int((time.time() - started) * 1000)
            event = {
                'ts': int(time.time() * 1000),
                'type': 'http',
                'target': TARGET_NAME,
                'method': request.method,
                'path': request.path,
                'query': request.query_string,
                'status': resp.status,
                'req_bytes': len(req_body),
                'resp_bytes': len(resp_body),
                'latency_ms': latency_ms,
                'upstream': UPSTREAM,
            }
            append_event(event)
            passthrough_headers = dict(resp.headers)
            passthrough_headers.pop('Transfer-Encoding', None)
            passthrough_headers.pop('Content-Length', None)
            return web.Response(status=resp.status, body=resp_body, headers=passthrough_headers)
    except ClientError as err:
        latency_ms = int((time.time() - started) * 1000)
        detail = {
            'target': TARGET_NAME,
            'method': request.method,
            'path': request.path,
            'query': request.query_string,
            'req_bytes': len(req_body),
            'latency_ms': latency_ms,
            'upstream': UPSTREAM,
            'err': str(err),
        }
        log_err('upstream_client_error', detail)
        return web.json_response({'ok': 0, 'err': 'proxy_upstream_client_error', 'detail': str(err)}, status=502)
    except asyncio.TimeoutError:
        latency_ms = int((time.time() - started) * 1000)
        detail = {
            'target': TARGET_NAME,
            'method': request.method,
            'path': request.path,
            'query': request.query_string,
            'req_bytes': len(req_body),
            'latency_ms': latency_ms,
            'upstream': UPSTREAM,
        }
        log_err('upstream_timeout', detail)
        return web.json_response({'ok': 0, 'err': 'proxy_upstream_timeout'}, status=504)
    except Exception as err:
        latency_ms = int((time.time() - started) * 1000)
        detail = {
            'target': TARGET_NAME,
            'method': request.method,
            'path': request.path,
            'query': request.query_string,
            'req_bytes': len(req_body),
            'latency_ms': latency_ms,
            'upstream': UPSTREAM,
            'err': str(err),
            'trace': traceback.format_exc()[-1600:],
        }
        log_err('proxy_internal_error', detail)
        return web.json_response({'ok': 0, 'err': 'proxy_internal_error', 'detail': str(err)}, status=500)

async def on_startup(app):
    timeout = ClientTimeout(total=PROXY_TIMEOUT_SEC)
    app['client'] = ClientSession(timeout=timeout)
    log(f'listen={LISTEN_HOST}:{LISTEN_PORT} upstream={UPSTREAM} target={TARGET_NAME} out={OUT_FILE}')

async def on_cleanup(app):
    await app['client'].close()
    log('shutdown complete')

def main():
    app = web.Application(client_max_size=MAX_BODY_MB * 1024 * 1024)
    app.router.add_get('/health', proxy_health)
    app.router.add_get('/__proxy/upstream_health', proxy_upstream_health)
    app.router.add_route('*', '/{tail:.*}', handle)
    app.on_startup.append(on_startup)
    app.on_cleanup.append(on_cleanup)
    web.run_app(app, host=LISTEN_HOST, port=LISTEN_PORT, access_log=None)

if __name__ == '__main__':
    try:
        main()
    except Exception as err:
        log_err('fatal', {'err': str(err), 'trace': traceback.format_exc()[-1600:]})
        raise
