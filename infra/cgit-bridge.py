#!/usr/bin/env python3
"""HTTP bridge for cgit.cgi. Passes raw bytes, strips /cgit.cgi prefix."""
import http.server, subprocess, urllib.parse

CGIT = '/var/www/cgit/cgit.cgi'
PORT = 8081

class Handler(http.server.BaseHTTPRequestHandler):
    def do_GET(self):
        parsed = urllib.parse.urlparse(self.path)
        path = parsed.path
        if path.startswith('/cgit.cgi'):
            path = path[len('/cgit.cgi'):] or '/'

        env = {
            'SCRIPT_NAME': '',
            'SCRIPT_FILENAME': CGIT,
            'QUERY_STRING': parsed.query,
            'REQUEST_METHOD': 'GET',
            'PATH_INFO': path,
            'REQUEST_URI': self.path,
            'SERVER_NAME': self.headers.get('Host', 'git.ihorzash.com'),
            'SERVER_PORT': str(PORT),
            'HTTP_HOST': self.headers.get('Host', 'git.ihorzash.com'),
        }
        try:
            proc = subprocess.run([CGIT], env=env, capture_output=True, timeout=15)
            raw = proc.stdout
            # cgit separates headers/body with \n\n
            sep = raw.find(b'\n\n')
            body = raw[sep + 2:] if sep >= 0 else raw
            status = 200
            if sep >= 0:
                for line in raw[:sep].split(b'\n'):
                    if line.lower().startswith(b'status:'):
                        try: status = int(line.split()[1])
                        except: pass
            self.send_response(status)
            self.send_header('Content-Type', 'text/html; charset=utf-8')
            self.end_headers()
            self.wfile.write(body)
        except Exception as e:
            self.send_error(500, str(e))

    def log_message(self, *args): pass

http.server.HTTPServer(('127.0.0.1', PORT), Handler).serve_forever()
