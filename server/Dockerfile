# syntax=docker/dockerfile:1

# ---- Stage 1: build the admin SPA ----
FROM node:20-alpine AS web
ARG NPM_REGISTRY=https://registry.npmmirror.com
WORKDIR /build
COPY server/web/admin/package.json ./
RUN npm config set registry ${NPM_REGISTRY} && npm install
COPY server/web/admin/ ./
RUN npm run build

# ---- Stage 2: build the Go server ----
FROM golang:1.22-bookworm AS server
ARG GOPROXY_URL=https://goproxy.cn,direct
WORKDIR /src
COPY server/ ./
COPY --from=web /build/dist ./web/admin/dist
RUN GOPROXY=${GOPROXY_URL} go mod tidy \
    && GOPROXY=${GOPROXY_URL} CGO_ENABLED=0 go build -trimpath -o /bin/server ./cmd/server

# ---- Stage 3: runtime ----
FROM alpine:3.20
ARG APK_MIRROR=https://mirrors.aliyun.com
RUN sed -i "s#https://dl-cdn.alpinelinux.org#${APK_MIRROR}#g" /etc/apk/repositories \
    && apk add --no-cache ca-certificates wget
COPY --from=server /bin/server /bin/server
COPY client/resources/miniapps /seed/miniapps
ENV NXKIT_DATA_DIR=/data
VOLUME /data
EXPOSE 8080
ENTRYPOINT ["/bin/server"]
