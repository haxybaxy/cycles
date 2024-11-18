#include "api.h"
#include "utils.h"
#include <iostream>
#include <queue>
#include <spdlog/spdlog.h>
#include <string>
#include <vector>

using namespace cycles;

class BotClient {
  Connection connection;
  std::string name;
  GameState state;
  Player my_player;
  std::mt19937 rng;
  std::vector<Direction> last_moves;  // Store last N moves
  const size_t move_history_size = 4; // Remember last 4 moves

  bool is_valid_move(Direction direction) {
    auto new_pos = my_player.position + getDirectionVector(direction);
    if (!state.isInsideGrid(new_pos)) {
      return false;
    }
    if (state.getGridCell(new_pos) != 0) {
      return false;
    }
    return true;
  }

  int floodFill(const sf::Vector2i& position, std::vector<std::vector<bool>>& visited) {
    int area = 0;
    std::queue<sf::Vector2i> q;
    q.push(position);
    visited[position.x][position.y] = true;

    static const std::vector<sf::Vector2i> directions = {
        {0, -1}, // North
        {1, 0},  // East
        {0, 1},  // South
        {-1, 0}  // West
    };

    while (!q.empty()) {
      sf::Vector2i current = q.front();
      q.pop();
      area++;

      for (const auto& dir : directions) {
        sf::Vector2i newPos = current + dir;
        if (state.isInsideGrid(newPos) &&
            state.getGridCell(newPos) == 0 &&
            !visited[newPos.x][newPos.y]) {
          visited[newPos.x][newPos.y] = true;
          q.push(newPos);
        }
      }
    }

    return area;
  }

  bool areOppositeDirections(Direction a, Direction b) {
    return (a == Direction::north && b == Direction::south) ||
           (a == Direction::south && b == Direction::north) ||
           (a == Direction::east && b == Direction::west) ||
           (a == Direction::west && b == Direction::east);
  }

  bool wouldFormPartialSquare(const std::vector<Direction>& history, Direction newMove) {
    if (history.size() < 2) return false;

    // Get the last two moves
    Direction lastMove = history.back();
    Direction secondLastMove = history[history.size() - 2];

    // Check for three identical moves in a row
    if (history.size() >= 2 && lastMove == secondLastMove && newMove == lastMove) {
        return true;
    }

    // Check for moves that would complete 3/4 of a square
    // Example: north -> east -> south or west -> north -> east
    if (lastMove != secondLastMove &&
        ((newMove == Direction::north && lastMove == Direction::east && secondLastMove == Direction::south) ||
         (newMove == Direction::east && lastMove == Direction::south && secondLastMove == Direction::west) ||
         (newMove == Direction::south && lastMove == Direction::west && secondLastMove == Direction::north) ||
         (newMove == Direction::west && lastMove == Direction::north && secondLastMove == Direction::east) ||
         (newMove == Direction::north && lastMove == Direction::west && secondLastMove == Direction::south) ||
         (newMove == Direction::east && lastMove == Direction::north && secondLastMove == Direction::west) ||
         (newMove == Direction::south && lastMove == Direction::east && secondLastMove == Direction::north) ||
         (newMove == Direction::west && lastMove == Direction::south && secondLastMove == Direction::east))) {
        return true;
    }

    return false;
  }

  // Add new helper method to check for potential traps
  bool isPathToFreedom(const sf::Vector2i& position, int minRequiredSpace = 10) {
    std::vector<std::vector<bool>> visited(state.gridWidth, std::vector<bool>(state.gridHeight, false));
    return floodFill(position, visited) >= minRequiredSpace;
  }

  Direction decideMove() {
    std::vector<Direction> directions = {
        Direction::north,
        Direction::east,
        Direction::south,
        Direction::west
    };

    int maxScore = -1;
    Direction bestDirection = Direction::north;

    for (auto direction : directions) {
        if (!is_valid_move(direction)) continue;

        // Increase penalty for moves that would form partial squares
        if (wouldFormPartialSquare(last_moves, direction)) {
            spdlog::debug("{}: Skipping direction {} to avoid partial square",
                         name, getDirectionValue(direction));
            continue;
        }

        sf::Vector2i newPos = my_player.position + getDirectionVector(direction);

        // Check if move leads to a dead end
        if (!isPathToFreedom(newPos, 15)) {
            spdlog::debug("{}: Skipping direction {} - leads to potential trap",
                         name, getDirectionValue(direction));
            continue;
        }

        // Calculate risk factors
        int adjacent_walls = 0;
        int diagonal_walls = 0;
        int player_walls = 0;  // New counter for nearby player trails

        // Check adjacent cells for walls and player trails
        for (const auto& dir : directions) {
            sf::Vector2i checkPos = newPos + getDirectionVector(dir);
            if (!state.isInsideGrid(checkPos)) {
                adjacent_walls++;
            } else if (state.getGridCell(checkPos) != 0) {
                adjacent_walls++;
                if (state.getGridCell(checkPos) == my_player.id) {
                    player_walls++;  // Extra penalty for own trails
                }
            }
        }

        // Check diagonal cells
        static const std::vector<sf::Vector2i> diagonals = {
            {-1, -1}, {1, -1}, {-1, 1}, {1, 1}
        };
        for (const auto& diag : diagonals) {
            sf::Vector2i checkPos = newPos + diag;
            if (!state.isInsideGrid(checkPos) || state.getGridCell(checkPos) != 0) {
                diagonal_walls++;
            }
        }

        // Initialize visited grid and perform flood fill
        std::vector<std::vector<bool>> visited(state.gridWidth, std::vector<bool>(state.gridHeight, false));
        for (int x = 0; x < state.gridWidth; ++x) {
            for (int y = 0; y < state.gridHeight; ++y) {
                if (state.getGridCell({x, y}) != 0) {
                    visited[x][y] = true;
                }
            }
        }

        int area = floodFill(newPos, visited);
        int score = area * 2;  // Increase weight of available space

        // Enhanced penalty system
        if (adjacent_walls >= 2) {
            score /= (adjacent_walls * 3);  // Increased penalty
        }

        if (player_walls > 0) {
            score /= (player_walls * 4);  // Heavy penalty for own trails
        }

        if (diagonal_walls >= 2) {
            score /= (diagonal_walls * 2);
        }

        // Stronger penalties for repetitive patterns
        if (!last_moves.empty()) {
            if (areOppositeDirections(direction, last_moves.back())) {
                score /= 8;  // Increased penalty for reversing
            }

            if (last_moves.size() >= 2) {
                int repetition_penalty = 0;
                for (size_t i = 0; i < last_moves.size() - 1; i++) {
                    if (last_moves[i] == direction) {
                        repetition_penalty += 5;  // Increased penalty
                    }
                }
                score = score / (1 + repetition_penalty);
            }
        }

        spdlog::debug("{}: Direction {} has score {} (area: {}, adj_walls: {}, diag_walls: {})",
                     name, getDirectionValue(direction), score, area, adjacent_walls, diagonal_walls);

        if (score > maxScore) {
            maxScore = score;
            bestDirection = direction;
        }
    }

    // Update move history
    last_moves.push_back(bestDirection);
    if (last_moves.size() > move_history_size) {
        last_moves.erase(last_moves.begin());
    }

    spdlog::debug("{}: Chose direction {} with score {}", name, getDirectionValue(bestDirection), maxScore);
    return bestDirection;
  }

  void receiveGameState() {
    state = connection.receiveGameState();
    for (const auto& player : state.players) {
      if (player.name == name) {
        my_player = player;
        break;
      }
    }
  }

  void sendMove() {
    spdlog::debug("{}: Sending move", name);
    auto move = decideMove();
    connection.sendMove(move);
  }

public:
  BotClient(const std::string& botName) : name(botName) {
    std::random_device rd;
    rng.seed(rd());
    connection.connect(name);
    if (!connection.isActive()) {
      spdlog::critical("{}: Connection failed", name);
      exit(1);
    }
  }

  void run() {
    while (connection.isActive()) {
      receiveGameState();
      sendMove();
    }
  }
};

int main(int argc, char* argv[]) {
  if (argc != 2) {
    std::cerr << "Usage: " << argv[0] << " <bot_name>" << std::endl;
    return 1;
  }
#if SPDLOG_ACTIVE_LEVEL == SPDLOG_LEVEL_TRACE
  spdlog::set_level(spdlog::level::debug);
#endif
  std::string botName = argv[1];
  BotClient bot(botName);
  bot.run();
  return 0;
}
