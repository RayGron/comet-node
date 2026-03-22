FROM node:20-bookworm-slim AS build

WORKDIR /workspace/ui/operator-react

COPY ui/operator-react/package.json ui/operator-react/package-lock.json ./
RUN npm ci

COPY ui/operator-react/ ./
RUN npm run build

FROM nginx:1.29-alpine

COPY runtime/web-ui/nginx.conf.template /etc/nginx/templates/default.conf.template
COPY --from=build /workspace/ui/operator-react/dist/ /usr/share/nginx/html/

EXPOSE 8080
