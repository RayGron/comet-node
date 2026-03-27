FROM nginx:1.29-alpine

COPY runtime/web-ui/nginx.conf.template /etc/nginx/templates/default.conf.template
COPY ui/operator-react/dist/ /usr/share/nginx/html/

EXPOSE 8080
