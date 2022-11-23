CREATE VIRTUAL TABLE IF NOT EXISTS content USING nadeko('./content');

CREATE VIEW IF NOT EXISTS routes (uri, content) AS
SELECT uri, 'HTTP/1.0 200 OK'                       || char(13)||char(10)
         || 'Content-Length: ' || length(body)      || char(13)||char(10)
         || (SELECT rendered FROM rendered_headers) || char(13)||char(10)
         || char(13)||char(10)
         || body
         || char(13)||char(10)
FROM static_files;

CREATE VIEW IF NOT EXISTS rendered_headers (rendered) AS
SELECT group_concat(key || ': ' || value, char(13)||char(10))
FROM headers;

CREATE TABLE IF NOT EXISTS static_files (uri, body);

INSERT INTO static_files (uri, body)
SELECT (substr(filename, length('./content/'))) AS uri,
       (CASE WHEN contents IS NULL THEN '' ELSE contents END) AS body
FROM content
WHERE uri LIKE '%.html';

INSERT INTO static_files (uri, body)
SELECT (replace(uri, '/index.html', '/')) AS uri, body
FROM static_files
WHERE uri LIKE '%/index.html';

CREATE TABLE IF NOT EXISTS headers (key, value);
INSERT INTO headers (key, value)
VALUES ('Server', 'tomie/0.1'),
       ('Connection', 'close'),
       ('Cache-Control', 'public, max-age=86400');
