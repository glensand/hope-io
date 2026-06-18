#!/bin/sh
# Generate self-signed certificates for TLS testing
openssl req -x509 -newkey rsa:2048 -keyout key.pem \
    -out cert.pem -days 3650 -nodes \
    -subj "/CN=localhost" 2>/dev/null
