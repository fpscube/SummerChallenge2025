import java.util.Properties;
import java.util.Map;
import java.util.Random;
import com.codingame.gameengine.runner.MultiplayerGameRunner;
import com.codingame.gameengine.runner.simulate.GameResult;


public class Main {
    public static void main(String[] args) {

        if (args.length < 3) {
            System.out.println("Usage: java Main <mode> <agent1_path> <agent2_path> [seed]");
            System.out.println("<mode>: 'start' for server mode, 'run' for console mode, 'stat' for statistics mode");
            return;
        }

        String mode = args[0];
        String agent1Path = args[1];
        String agent2Path = args[2];

        Long seed = null;
        if (args.length >= 4) {
            try {
                seed = Long.parseLong(args[3]);
            } catch (NumberFormatException e) {
                System.out.println("Invalid seed. Please provide a valid long value.");
                return;
            }
        }

        // Choose mode
        if ("start".equalsIgnoreCase(mode)) {
            // Start game on server mode
            MultiplayerGameRunner gameRunner = new MultiplayerGameRunner();
            gameRunner.addAgent(agent1Path);
            gameRunner.addAgent(agent2Path);
            gameRunner.setLeagueLevel(5);
            if (seed != null) {
                gameRunner.setSeed(seed);
            }
            gameRunner.start(8888);
        } else if ("run".equalsIgnoreCase(mode)) {
            // Run game in console mode
            MultiplayerGameRunner gameRunner = new MultiplayerGameRunner();
            gameRunner.addAgent(agent1Path);
            gameRunner.addAgent(agent2Path);
            gameRunner.setLeagueLevel(5);
            if (seed != null) {
                gameRunner.setSeed(seed);
            }
            GameResult result = gameRunner.simulate();

            // Display results in console
            System.out.println("Game finished!");
            System.out.println("Scores:");
            for (Map.Entry<Integer, Integer> entry : result.scores.entrySet()) {
                System.out.printf("Agent %d: %d\n", entry.getKey(), entry.getValue());
            }
        } else if ("stat".equalsIgnoreCase(mode)) {
            // Statistics mode
            int totalMatches = 200;
            int agent1Wins = 0;
            int agent2Wins = 0;
            int ties = 0;

            Random random = new Random(12345L);

            for (int i = 0; i < totalMatches; i++) {
                long randomSeed = random.nextLong();

                // Create a new game runner for the first match
                MultiplayerGameRunner gameRunner = new MultiplayerGameRunner();
                gameRunner.addAgent(agent1Path);
                gameRunner.addAgent(agent2Path);
                gameRunner.setLeagueLevel(5);
                gameRunner.setSeed(randomSeed);

                GameResult result = gameRunner.simulate();

                int score1 = result.scores.getOrDefault(0, 0);
                int score2 = result.scores.getOrDefault(1, 0);

                if (score1 > score2) {
                    agent1Wins++;
                } else if (score2 > score1) {
                    agent2Wins++;
                } else {
                    ties++;
                }

                System.out.printf("Agent1 Win%%=%.2f, Agent2 Win%%=%.2f, Tie%%=%.2f | Match1 %d: Seed=%d, Agent1 Score=%d, Agent2 Score=%d\n", 
                    (agent1Wins * 100.0) / (i*2 + 1), 
                    (agent2Wins * 100.0) / (i*2 + 1), 
                    (ties * 100.0) / (i*2 + 1),
                    i*2 + 1, randomSeed, score1, score2);

                // Create a new game runner for the reverse match
                MultiplayerGameRunner reverseGameRunner = new MultiplayerGameRunner();
                reverseGameRunner.addAgent(agent2Path);
                reverseGameRunner.addAgent(agent1Path);
                reverseGameRunner.setLeagueLevel(5);
                reverseGameRunner.setSeed(randomSeed);

                GameResult reverseResult = reverseGameRunner.simulate();

                score2 = reverseResult.scores.getOrDefault(0, 0);
                score1 = reverseResult.scores.getOrDefault(1, 0);
              
                if (score1 > score2) {
                    agent1Wins++;
                } else if (score2 > score1) {
                    agent2Wins++;
                } else {
                    ties++;
                }

                System.out.printf("Agent1 Win%%=%.2f, Agent2 Win%%=%.2f, Tie%%=%.2f | Match2 %d: Seed=%d, Agent1 Score=%d, Agent2 Score=%d\n", 
                    (agent1Wins * 100.0) / (i*2 + 2), 
                    (agent2Wins * 100.0) / (i*2 + 2), 
                    (ties * 100.0) / (i*2 + 2),
                    i*2 + 2, randomSeed, score1, score2);
            }

            System.out.println("\nFinal Statistics:");
            System.out.printf("Agent 1 Win Percentage: %.2f%%\n", (agent1Wins * 100.0) / (2 * totalMatches));
            System.out.printf("Agent 2 Win Percentage: %.2f%%\n", (agent2Wins * 100.0) / (2 * totalMatches));
            System.out.printf("Tie Percentage: %.2f%%\n", (ties * 100.0) / (2 * totalMatches));
        } else {
            System.out.println("Invalid mode. Use 'start' for server mode, 'run' for console mode, or 'stat' for statistics mode.");
        }
    
    }
}


