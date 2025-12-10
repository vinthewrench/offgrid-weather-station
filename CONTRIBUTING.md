# Contributing

Thanks for your interest in improving this project. The goal is to keep the
codebase simple, understandable, and useful for anyone running an Ecowitt
WS90 and the lightweight backend/dashboard included here.

## How to Contribute

1. Fork this repository.
2. Create a feature branch describing what you're working on.
3. Make your changes in small, clean commits.
4. Submit a pull request with a clear explanation of the change.

## Code Style

- Keep things simple.
- Avoid unnecessary dependencies.
- Follow the existing formatting and naming patterns.
- Frontend code (HTML/JS/CSS) should stay readable and lightweight.
- Backend code added later should include comments where logic is not obvious.

## Running the Project

You can stand up the full system with Docker:

```
docker compose up --build
```

Static files live in `html/`.  
The backend runs at `weather-backend-v2:8889`.

## Reporting Issues

If something breaks, open an issue and include:

- Steps to reproduce
- Logs if relevant
- Expected vs actual behavior

## Licensing

By submitting a pull request, you agree that your contribution is provided
under the MIT License that covers this repository.
