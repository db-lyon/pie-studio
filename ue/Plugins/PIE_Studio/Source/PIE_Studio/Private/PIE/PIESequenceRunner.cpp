#include "PIE/PIESequenceRunner.h"
#include "PIE/PIEActorPuppet.h"
#include "PIE_StudioModule.h"
#include "Editor.h"
#include "Engine/World.h"
#include "GameFramework/Actor.h"

namespace UEMCPPIE
{
	namespace
	{
		bool SeqStepObject(const TSharedPtr<FJsonValue>& V, TSharedPtr<FJsonObject>& Out)
		{
			const TSharedPtr<FJsonObject>* O = nullptr;
			if (V.IsValid() && V->TryGetObject(O) && O) { Out = *O; return true; }
			return false;
		}

		FString SeqStepKind(const TSharedPtr<FJsonObject>& O)
		{
			FString Kind;
			O->TryGetStringField(TEXT("kind"), Kind);
			return Kind.ToLower();
		}

		void SeqReadVec3(const TSharedPtr<FJsonObject>& O, const TCHAR* Field, double& X, double& Y, double& Z)
		{
			const TArray<TSharedPtr<FJsonValue>>* Arr = nullptr;
			if (!O->TryGetArrayField(Field, Arr) || !Arr) return;
			if (Arr->Num() > 0 && (*Arr)[0].IsValid()) X = (*Arr)[0]->AsNumber();
			if (Arr->Num() > 1 && (*Arr)[1].IsValid()) Y = (*Arr)[1]->AsNumber();
			if (Arr->Num() > 2 && (*Arr)[2].IsValid()) Z = (*Arr)[2]->AsNumber();
		}

		bool ParseCondOp(const FString& Raw, EStepCondOp& Out)
		{
			const FString Op = Raw.ToLower().Replace(TEXT("_"), TEXT(""));
			if (Op.IsEmpty() || Op == TEXT("istrue") || Op == TEXT("true"))   { Out = EStepCondOp::IsTrue; return true; }
			if (Op == TEXT("isfalse") || Op == TEXT("false"))                 { Out = EStepCondOp::IsFalse; return true; }
			if (Op == TEXT("eq") || Op == TEXT("=="))                         { Out = EStepCondOp::Eq; return true; }
			if (Op == TEXT("ne") || Op == TEXT("!="))                         { Out = EStepCondOp::Ne; return true; }
			if (Op == TEXT("lt") || Op == TEXT("<"))                          { Out = EStepCondOp::Lt; return true; }
			if (Op == TEXT("lte") || Op == TEXT("<="))                        { Out = EStepCondOp::Lte; return true; }
			if (Op == TEXT("gt") || Op == TEXT(">"))                          { Out = EStepCondOp::Gt; return true; }
			if (Op == TEXT("gte") || Op == TEXT(">="))                        { Out = EStepCondOp::Gte; return true; }
			return false;
		}

		// Truthiness for the no-value operators: a bool is itself, a number is
		// non-zero, a string is non-empty, null is false.
		bool JsonTruthy(const TSharedPtr<FJsonValue>& V)
		{
			if (!V.IsValid()) return false;
			switch (V->Type)
			{
			case EJson::Boolean: return V->AsBool();
			case EJson::Number:  return !FMath::IsNearlyZero(V->AsNumber());
			case EJson::String:  return !V->AsString().IsEmpty();
			case EJson::Null:    return false;
			default:             return true;
			}
		}

		bool JsonToNumber(const TSharedPtr<FJsonValue>& V, double& Out)
		{
			if (!V.IsValid()) return false;
			if (V->Type == EJson::Number)  { Out = V->AsNumber(); return true; }
			if (V->Type == EJson::Boolean) { Out = V->AsBool() ? 1.0 : 0.0; return true; }
			return false;
		}

		bool CompareJson(const TSharedPtr<FJsonValue>& Actual, const TSharedPtr<FJsonValue>& Expected,
		                 EStepCondOp Op, double Tolerance, bool& bOutMet, FString& OutError)
		{
			if (Op == EStepCondOp::IsTrue)  { bOutMet = JsonTruthy(Actual); return true; }
			if (Op == EStepCondOp::IsFalse) { bOutMet = !JsonTruthy(Actual); return true; }

			if (!Expected.IsValid())
			{
				OutError = TEXT("condition needs 'value' for this operator");
				return false;
			}

			double A = 0.0, B = 0.0;
			if (JsonToNumber(Actual, A) && JsonToNumber(Expected, B))
			{
				switch (Op)
				{
				case EStepCondOp::Eq:  bOutMet = FMath::Abs(A - B) <= Tolerance; break;
				case EStepCondOp::Ne:  bOutMet = FMath::Abs(A - B) > Tolerance;  break;
				case EStepCondOp::Lt:  bOutMet = A < B;   break;
				case EStepCondOp::Lte: bOutMet = A <= B;  break;
				case EStepCondOp::Gt:  bOutMet = A > B;   break;
				case EStepCondOp::Gte: bOutMet = A >= B;  break;
				default: bOutMet = false; break;
				}
				return true;
			}

			// Non-numeric values only support equality, and only as text.
			if (Op != EStepCondOp::Eq && Op != EStepCondOp::Ne)
			{
				OutError = TEXT("ordering operators need numeric values");
				return false;
			}
			FString AS, BS;
			if (Actual.IsValid()) Actual->TryGetString(AS);
			Expected->TryGetString(BS);
			const bool bSame = AS.Equals(BS, ESearchCase::IgnoreCase);
			bOutMet = (Op == EStepCondOp::Eq) ? bSame : !bSame;
			return true;
		}

		TSharedRef<FJsonObject> StepEntry(int32 Index, const FString& Kind)
		{
			TSharedRef<FJsonObject> E = MakeShared<FJsonObject>();
			E->SetNumberField(TEXT("index"), Index);
			E->SetStringField(TEXT("kind"), Kind);
			return E;
		}
	}

	bool FStepCondition::Parse(const TSharedPtr<FJsonObject>& O, bool bNegate, FStepCondition& Out, FString& OutError)
	{
		if (!O.IsValid()) { OutError = TEXT("condition is not an object"); return false; }

		Out.bSet = true;
		Out.bNegated = bNegate;
		Out.Target = FObjectAddress::FromJson(O);

		if (!O->TryGetStringField(TEXT("property"), Out.Property) || Out.Property.IsEmpty())
		{
			OutError = TEXT("condition needs 'property'");
			return false;
		}

		FString RawOp;
		O->TryGetStringField(TEXT("op"), RawOp);
		if (!ParseCondOp(RawOp, Out.Op))
		{
			OutError = FString::Printf(TEXT("unknown op '%s' (eq|ne|lt|lte|gt|gte|is_true|is_false)"), *RawOp);
			return false;
		}

		Out.Value = O->Values.FindRef(TEXT("value"));
		Out.Tolerance = 0.0;
		O->TryGetNumberField(TEXT("tolerance"), Out.Tolerance);

		if (Out.Op != EStepCondOp::IsTrue && Out.Op != EStepCondOp::IsFalse && !Out.Value.IsValid())
		{
			OutError = FString::Printf(TEXT("op '%s' needs 'value'"), *RawOp);
			return false;
		}
		return true;
	}

	FString FStepCondition::Describe() const
	{
		return FString::Printf(TEXT("%s%s.%s"),
			bNegated ? TEXT("until ") : TEXT("while "),
			*Target.Describe(), *Property);
	}

	UObject* FPIESequenceRunner::ResolveTarget(FSequenceContext& Ctx, const FObjectAddress& Addr, FString& OutError)
	{
		return FPIEObjectAddress::Resolve(Ctx.World, Addr, &Ctx.Refs, OutError);
	}

	void FPIESequenceRunner::Capture(FSequenceContext& Ctx, const FString& Name,
	                                 const TSharedPtr<FJsonValue>& Value, UObject* Object)
	{
		if (Name.IsEmpty()) return;
		Ctx.Captured.Add(Name, Value.IsValid() ? Value : MakeShared<FJsonValueNull>());
		if (Object) Ctx.Refs.Add(Name, Object);
	}

	bool FPIESequenceRunner::EvaluateCondition(FSequenceContext& Ctx, const FStepCondition& Cond,
	                                           bool& bOutMet, FString& OutError)
	{
		UObject* Target = ResolveTarget(Ctx, Cond.Target, OutError);
		if (!Target) return false;

		TSharedPtr<FJsonValue> Actual;
		if (!FPIEActorPuppet::GetPropertyByPath(Target, Cond.Property, Actual, OutError)) return false;

		bool bMet = false;
		if (!CompareJson(Actual, Cond.Value, Cond.Op, Cond.Tolerance, bMet, OutError)) return false;

		bOutMet = Cond.bNegated ? !bMet : bMet;
		return true;
	}

	bool FPIESequenceRunner::Validate(const TArray<TSharedPtr<FJsonValue>>& Steps, TArray<FString>& OutErrors)
	{
		if (Steps.Num() == 0) OutErrors.Add(TEXT("no steps"));
		if (Steps.Num() > kMaxSteps)
		{
			OutErrors.Add(FString::Printf(TEXT("too many steps (%d, max %d)"), Steps.Num(), kMaxSteps));
		}

		for (int32 i = 0; i < Steps.Num(); ++i)
		{
			TSharedPtr<FJsonObject> O;
			if (!SeqStepObject(Steps[i], O))
			{
				OutErrors.Add(FString::Printf(TEXT("step[%d] is not an object"), i));
				continue;
			}
			const FString Kind = SeqStepKind(O);

			if (Kind == TEXT("spawn"))
			{
				FString C;
				if (!O->TryGetStringField(TEXT("class"), C) || C.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("step[%d] spawn needs 'class'"), i));
			}
			else if (Kind == TEXT("set"))
			{
				FString Path;
				if (FObjectAddress::FromJson(O).IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("step[%d] set needs a target"), i));
				if (!O->TryGetStringField(TEXT("path"), Path) || Path.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("step[%d] set needs 'path'"), i));
				if (!O->HasField(TEXT("value")))
					OutErrors.Add(FString::Printf(TEXT("step[%d] set needs 'value'"), i));
			}
			else if (Kind == TEXT("call"))
			{
				FString Func;
				if (FObjectAddress::FromJson(O).IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("step[%d] call needs a target"), i));
				if (!O->TryGetStringField(TEXT("func"), Func) || Func.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("step[%d] call needs 'func'"), i));
			}
			else if (Kind == TEXT("read"))
			{
				FString Prop;
				if (FObjectAddress::FromJson(O).IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("step[%d] read needs a target"), i));
				if (!O->TryGetStringField(TEXT("property"), Prop) || Prop.IsEmpty())
					OutErrors.Add(FString::Printf(TEXT("step[%d] read needs 'property'"), i));
			}
			else if (Kind == TEXT("loop"))
			{
				// The cap is mandatory and bounded. A loop whose predicate never
				// clears would otherwise hang the game thread inside one dispatch,
				// which is exactly the thing this runner promises not to do.
				double MaxIter = 0.0;
				if (!O->TryGetNumberField(TEXT("max_iterations"), MaxIter))
				{
					OutErrors.Add(FString::Printf(TEXT("step[%d] loop needs 'max_iterations'"), i));
				}
				else if (MaxIter < 1.0 || MaxIter > static_cast<double>(kMaxLoopIterations))
				{
					OutErrors.Add(FString::Printf(TEXT("step[%d] loop 'max_iterations' must be 1..%d"), i, kMaxLoopIterations));
				}

				const TSharedPtr<FJsonObject>* CondObj = nullptr;
				const bool bHasWhile = O->TryGetObjectField(TEXT("while"), CondObj) && CondObj;
				const TSharedPtr<FJsonObject>* UntilObj = nullptr;
				const bool bHasUntil = O->TryGetObjectField(TEXT("until"), UntilObj) && UntilObj;
				if (bHasWhile && bHasUntil)
				{
					OutErrors.Add(FString::Printf(TEXT("step[%d] loop names both 'while' and 'until'; pick one"), i));
				}
				else if (bHasWhile || bHasUntil)
				{
					FStepCondition Cond;
					FString Err;
					if (!FStepCondition::Parse(bHasWhile ? *CondObj : *UntilObj, bHasUntil, Cond, Err))
					{
						OutErrors.Add(FString::Printf(TEXT("step[%d] loop condition: %s"), i, *Err));
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Body = nullptr;
				if (!O->TryGetArrayField(TEXT("steps"), Body) || !Body || Body->Num() == 0)
				{
					OutErrors.Add(FString::Printf(TEXT("step[%d] loop needs a non-empty 'steps' body"), i));
				}
				else
				{
					TArray<FString> Inner;
					Validate(*Body, Inner);
					for (const FString& E : Inner)
					{
						OutErrors.Add(FString::Printf(TEXT("step[%d] loop body: %s"), i, *E));
					}
				}
			}
			else
			{
				OutErrors.Add(FString::Printf(TEXT("step[%d] unknown kind '%s' (spawn|set|call|read|loop)"), i, *Kind));
			}
		}

		return OutErrors.Num() == 0;
	}

	bool FPIESequenceRunner::Run(FSequenceContext& Ctx, const TArray<TSharedPtr<FJsonValue>>& Steps,
	                             bool bAbortOnError, TArray<TSharedPtr<FJsonValue>>& OutLog, FString& OutError)
	{
		for (int32 i = 0; i < Steps.Num(); ++i)
		{
			if (Ctx.Budget <= 0)
			{
				OutError = FString::Printf(TEXT("step budget exhausted (%d executions)"), kMaxStepExecutions);
				return false;
			}
			--Ctx.Budget;

			// The world cannot change under us mid-dispatch, but a step's own call
			// can end the game. Say so plainly instead of crashing on the next step.
			if (!GEditor || GEditor->PlayWorld != Ctx.World)
			{
				OutError = TEXT("the PIE session ended part way through the sequence");
				return false;
			}

			TSharedPtr<FJsonObject> O;
			if (!SeqStepObject(Steps[i], O))
			{
				OutError = FString::Printf(TEXT("step[%d] is not an object"), i);
				return false;
			}
			const FString Kind = SeqStepKind(O);
			FString CaptureName;
			O->TryGetStringField(TEXT("capture"), CaptureName);

			TSharedRef<FJsonObject> Entry = StepEntry(i, Kind);
			FString StepError;
			bool bStepOk = false;

			if (Kind == TEXT("spawn"))
			{
				FString Class;
				O->TryGetStringField(TEXT("class"), Class);
				double X = 0, Y = 0, Z = 0, P = 0, Yaw = 0, R = 0, SX = 1, SY = 1, SZ = 1;
				SeqReadVec3(O, TEXT("at"), X, Y, Z);
				SeqReadVec3(O, TEXT("rotation"), P, Yaw, R);
				SeqReadVec3(O, TEXT("scale"), SX, SY, SZ);
				const FTransform Xform(FRotator(P, Yaw, R), FVector(X, Y, Z), FVector(SX, SY, SZ));

				AActor* Spawned = FPIEActorPuppet::SpawnActor(Ctx.World, Class, Xform, StepError);
				if (Spawned)
				{
					bStepOk = true;
					Entry->SetStringField(TEXT("id"), Spawned->GetName());
					Entry->SetStringField(TEXT("actor_path"), Spawned->GetPathName());
					Capture(Ctx, CaptureName, MakeShared<FJsonValueString>(Spawned->GetName()), Spawned);
				}
			}
			else if (Kind == TEXT("set"))
			{
				const FObjectAddress Addr = FObjectAddress::FromJson(O);
				FString Path;
				O->TryGetStringField(TEXT("path"), Path);
				if (UObject* Target = ResolveTarget(Ctx, Addr, StepError))
				{
					Entry->SetStringField(TEXT("target"), Target->GetName());
					Entry->SetStringField(TEXT("path"), Path);
					bStepOk = FPIEActorPuppet::SetPropertyByPath(Target, Path, O->Values.FindRef(TEXT("value")), StepError);
				}
			}
			else if (Kind == TEXT("call"))
			{
				const FObjectAddress Addr = FObjectAddress::FromJson(O);
				FString Func;
				O->TryGetStringField(TEXT("func"), Func);
				TArray<TSharedPtr<FJsonValue>> Args;
				const TArray<TSharedPtr<FJsonValue>>* ArgArr = nullptr;
				if (O->TryGetArrayField(TEXT("args"), ArgArr) && ArgArr) Args = *ArgArr;

				if (UObject* Target = ResolveTarget(Ctx, Addr, StepError))
				{
					Entry->SetStringField(TEXT("target"), Target->GetName());
					Entry->SetStringField(TEXT("func"), Func);
					TSharedPtr<FJsonValue> Returned;
					UObject* ReturnedObject = nullptr;
					bStepOk = FPIEActorPuppet::CallFunctionWithResult(Target, Func, Args, Returned, StepError, &ReturnedObject);
					if (bStepOk)
					{
						if (Returned.IsValid()) Entry->SetField(TEXT("result"), Returned);
						Capture(Ctx, CaptureName, Returned, ReturnedObject);
					}
				}
			}
			else if (Kind == TEXT("read"))
			{
				const FObjectAddress Addr = FObjectAddress::FromJson(O);
				FString Prop;
				O->TryGetStringField(TEXT("property"), Prop);
				if (UObject* Target = ResolveTarget(Ctx, Addr, StepError))
				{
					Entry->SetStringField(TEXT("target"), Target->GetName());
					Entry->SetStringField(TEXT("property"), Prop);
					TSharedPtr<FJsonValue> Value;
					bStepOk = FPIEActorPuppet::GetPropertyByPath(Target, Prop, Value, StepError);
					if (bStepOk)
					{
						Entry->SetField(TEXT("value"), Value);
						// An object-valued read is captured as the object too, so a
						// later step can be pointed at what this one found.
						FString Ignored;
						UObject* Pointed = FPIEActorPuppet::ResolveObjectByPath(Target, Prop, Ignored);
						Capture(Ctx, CaptureName, Value, Pointed);
					}
				}
			}
			else if (Kind == TEXT("loop"))
			{
				double MaxIterRaw = 0.0;
				O->TryGetNumberField(TEXT("max_iterations"), MaxIterRaw);
				const int32 MaxIter = FMath::Clamp(static_cast<int32>(MaxIterRaw), 1, kMaxLoopIterations);

				const TSharedPtr<FJsonObject>* WhileObj = nullptr;
				const TSharedPtr<FJsonObject>* UntilObj = nullptr;
				const bool bHasWhile = O->TryGetObjectField(TEXT("while"), WhileObj) && WhileObj;
				const bool bHasUntil = !bHasWhile && O->TryGetObjectField(TEXT("until"), UntilObj) && UntilObj;

				FStepCondition Cond;
				if (bHasWhile || bHasUntil)
				{
					if (!FStepCondition::Parse(bHasWhile ? *WhileObj : *UntilObj, bHasUntil, Cond, StepError))
					{
						Cond.bSet = false;
					}
				}

				const TArray<TSharedPtr<FJsonValue>>* Body = nullptr;
				O->TryGetArrayField(TEXT("steps"), Body);

				int32 Iterations = 0;
				FString StoppedBecause = TEXT("max_iterations");
				TArray<TSharedPtr<FJsonValue>> BodyLog;
				bStepOk = StepError.IsEmpty() && Body != nullptr;
				if (!Body) StepError = TEXT("loop has no 'steps' body");

				while (bStepOk && Iterations < MaxIter)
				{
					if (Cond.bSet)
					{
						bool bMet = false;
						if (!EvaluateCondition(Ctx, Cond, bMet, StepError))
						{
							bStepOk = false;
							StoppedBecause = TEXT("condition_error");
							break;
						}
						if (!bMet)
						{
							StoppedBecause = TEXT("condition");
							break;
						}
					}

					++Iterations;
					if (!Run(Ctx, *Body, bAbortOnError, BodyLog, StepError))
					{
						bStepOk = false;
						StoppedBecause = TEXT("body_error");
						break;
					}
				}

				// A predicate-driven loop that ran out of iterations did not finish
				// the thing it was stepping, so it says which of the two bounds ended
				// it rather than reporting a clean stop either way.
				Entry->SetNumberField(TEXT("iterations"), Iterations);
				Entry->SetNumberField(TEXT("max_iterations"), MaxIter);
				Entry->SetStringField(TEXT("stopped_because"), StoppedBecause);
				if (Cond.bSet) Entry->SetStringField(TEXT("condition"), Cond.Describe());
				Entry->SetArrayField(TEXT("steps"), BodyLog);
			}
			else
			{
				StepError = FString::Printf(TEXT("unknown step kind '%s' (spawn|set|call|read|loop)"), *Kind);
			}

			Entry->SetBoolField(TEXT("ok"), bStepOk);
			if (!bStepOk && !StepError.IsEmpty()) Entry->SetStringField(TEXT("error"), StepError);
			OutLog.Add(MakeShared<FJsonValueObject>(Entry));
			++Ctx.StepsRun;

			if (!bStepOk && bAbortOnError)
			{
				OutError = FString::Printf(TEXT("step[%d] %s: %s"), i, *Kind, *StepError);
				return false;
			}
		}
		return true;
	}
}
