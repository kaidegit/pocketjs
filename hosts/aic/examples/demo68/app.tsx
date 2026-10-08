import { createSignal } from "solid-js";
import { Text, View } from "@pocketjs/framework/solid/components";

export default function App() {
  const [count, setCount] = createSignal(0);
  const steps = [1, -1, 10];

  return (
    <View class="w-full h-full bg-slate-950 items-center justify-center">
      <Text class="text-2xl text-white font-bold">PocketJS · ArtInChip D12x</Text>
      <Text class="text-lg text-slate-400">{`count ${count()}`}</Text>
      <View class="flex-row items-center gap-3 mt-1">
        {steps.map((step) => (
          <View
            class="w-40 h-16 rounded-lg bg-blue-600 items-center justify-center"
            onPress={() => setCount(count() + step)}
          >
            <Text class="text-lg text-white font-bold">
              {step > 0 ? `+${step}` : `${step}`}
            </Text>
          </View>
        ))}
      </View>
    </View>
  );
}
