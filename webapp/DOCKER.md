# Docker Setup for Capsule Radar Webapp

## Production Build

Build and run the optimized production version:

```bash
# Build the image
docker build -t capsule-radar:latest .

# Run the container
docker run -p 5173:5173 capsule-radar:latest

# Or use docker-compose
docker-compose up -d
```

The app will be served on `http://localhost:5173` using the optimized built version.

## Development Mode

Run with hot reload for active development:

```bash
# Using docker-compose with volume mounts
docker-compose -f docker-compose.dev.yml up

# Or build and run manually
docker build -f Dockerfile.dev -t capsule-radar:dev .
docker run -it -p 5173:5173 -v $(pwd):/app capsule-radar:dev
```

The dev server will be available on `http://localhost:5173` with hot module replacement.

## Stopping Containers

```bash
# Stop and remove containers
docker-compose down

# Or for dev
docker-compose -f docker-compose.dev.yml down
```

## Notes

- API calls to `api.airplanes.live` work from within the container (external network access)
- Port 5173 must be available on your host
- Volume mounts in dev mode (`docker-compose.dev.yml`) sync all file changes
- Production build uses `serve` to run the built files
- Dev build uses Vite dev server with HMR (hot module reload)
