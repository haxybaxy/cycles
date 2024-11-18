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

  Direction decideMove() {
    std::vector<Direction> directions = {
        Direction::north,
        Direction::east,
        Direction::south,
        Direction::west
    };

    int maxArea = -1;
    Direction bestDirection = Direction::north; // Default to north if no moves are valid

    for (auto direction : directions) {
      if (!is_valid_move(direction)) continue;

      sf::Vector2i newPos = my_player.position + getDirectionVector(direction);

      // Initialize the visited grid
      std::vector<std::vector<bool>> visited(state.gridWidth, std::vector<bool>(state.gridHeight, false));

      // Mark occupied cells as visited
      for (int x = 0; x < state.gridWidth; ++x) {
        for (int y = 0; y < state.gridHeight; ++y) {
          if (state.getGridCell({x, y}) != 0) {
            visited[x][y] = true;
          }
        }
      }

      // Perform flood fill from the new position
      int area = floodFill(newPos, visited);

      spdlog::debug("{}: Direction {} has reachable area {}", name, getDirectionValue(direction), area);

      if (area > maxArea) {
        maxArea = area;
        bestDirection = direction;
      }
    }

    // If no valid moves are found (surrounded), stay in the current direction or pick any valid move
    if (maxArea == -1) {
      for (auto direction : directions) {
        if (is_valid_move(direction)) {
          bestDirection = direction;
          break;
        }
      }
    }

    spdlog::debug("{}: Chose direction {} with max area {}", name, getDirectionValue(bestDirection), maxArea);
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
